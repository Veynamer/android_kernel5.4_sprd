/*
 * Copyright (C) 2021 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/sipc.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "sprd_actions_queue.h"
#include "sblock.h"
#include "sipc_priv.h"

#define SLOG_POLL_ALL	(0xffff)
#define MAX_RX_BLOCK	8
#define BLK_POOL_CNT	2000
#define PENDING_CNT	100
#define SEND_CNT	80

/* actions */
enum {
	SLOG_READ_DATA_ACTION = 0,
	SLOG_CAN_WRITE_ACTION
};

struct slog_bridge;

/* sblock callback  struct*/
struct slog_cb {
	u32			index;
	struct slog_bridge	*sb;
	struct sblock_mgr	*sblock;
};

/* sblock bridge action struct */
struct slog_action {
	void	*actions;
	char	name[20];
	struct slog_bridge	*sb;
};

/* slog_bridge struct */
struct slog_bridge {
	u8	dst_tx;
	u8	ch_tx;
	u8	dst_rx[MAX_RX_BLOCK];
	u8	ch_rx[MAX_RX_BLOCK];

	u32	priority;
	u32	rx_cnt;
	u32	rx_block_max_size;
	u32	rx_blks_cnt;/* tx bigger, 1 tx blocks can read rx blocks cnt. */
	u32 send_cnt;
	u32 pending_cnt;
	u32 total_size;
	u32 write_failed_cnt;

	bool	tx_is_usb;
	bool	wait_log_enable;

	struct timer_list	flush_timer; /* flush the smem log to usb. */

	struct sblock_mgr	*sblock_rx[MAX_RX_BLOCK];
	struct sblock_mgr	*sblock_tx;

	struct slog_cb		log_cb_tx;
	struct slog_cb		log_cb_rx[MAX_RX_BLOCK];

	struct slog_action	log_action;

	struct device *dev;
	struct work_struct register_work;
	struct list_head pool_list;
	struct list_head send_list;
	struct list_head pending_list;
	spinlock_t	list_lock;
};

struct block_node {
	void	*vaddr;
	void	*addr;
	u32	length;
	u32	send_len;
	u8	dst;
	u8	channel;
	struct	list_head list;
};

#ifdef CONFIG_USB_F_VSERIAL_BYPASS_USER
extern ssize_t vser_pass_user_write(char *buf, size_t count);
extern void kernel_vser_register_callback(void *function, void *p);
extern void kernel_vser_set_pass_mode(bool pass);
#else
static ssize_t vser_pass_user_write(char *buf, size_t count) { return count; }
static void kernel_vser_register_callback(void *function, void *p) {}
static void kernel_vser_set_pass_mode(bool pass) {}
#endif

struct slog_bridge *sb_record;
EXPORT_SYMBOL_GPL(sb_record);

/* if log_transport the log */
static ushort log_transport;
module_param_named(log_transport, log_transport, ushort, 0644);

#define SLOG_FLUSH_TIMER	msecs_to_jiffies(20)
#define FAILED_CNT 2000

static void slog_bridge_flush_timer_fn(struct timer_list *timer)
{
	struct slog_bridge *sb = container_of(timer, struct slog_bridge, flush_timer);

	dev_dbg(sb->dev, "slog_bridge_flush_timer_fn:add SLOG_READ_DATA_ACTION");
	sprd_add_action_ex(sb->log_action.actions,
			   SLOG_READ_DATA_ACTION,
			   (int)SLOG_POLL_ALL);
}

static void slog_block_list_init(struct slog_bridge *sb)
{
	u32 i;
	struct block_node *blk;

	INIT_LIST_HEAD(&sb->pool_list);
	INIT_LIST_HEAD(&sb->send_list);
	INIT_LIST_HEAD(&sb->pending_list);

	for (i = 0; i < BLK_POOL_CNT; i++) {
		blk = devm_kzalloc(sb->dev, sizeof(*blk), GFP_KERNEL);
		if (blk)
			list_add_tail(&blk->list, &sb->pool_list);
	}
}

static void slog_block_list_free(struct slog_bridge *sb)
{
	struct block_node *blk, *temp;

	list_for_each_entry_safe(blk,
				 temp,
				 &sb->pool_list,
				 list) {
		list_del(&blk->list);
		kfree(blk);
	}

	list_for_each_entry_safe(blk,
				 temp,
				 &sb->send_list,
				 list) {
		list_del(&blk->list);
		kfree(blk);
	}

	list_for_each_entry_safe(blk,
				 temp,
				 &sb->pending_list,
				 list) {
		list_del(&blk->list);
		kfree(blk);
	}
}

static int slog_block_release(struct slog_bridge *sb, struct block_node *blk,
			       unsigned int len)
{
	struct sblock blk_rx;
	unsigned long flags;
	int rval;

	/* release this block */
	blk_rx.addr = blk->addr;
	blk_rx.length = blk->length;
	rval = sblock_release(blk->dst, blk->channel, &blk_rx);
	if (rval < 0) {
		pr_err("%s: slog-%d-%d release failed\n", __func__, blk->dst, blk->channel);
	}
	blk->send_len += len;
	/*usb may not write all buffer in a block*/
	if (blk->length > blk->send_len)
		pr_err("%s: usb write err, len =%d", __func__, len);

	dev_dbg(sb->dev, "%s: blk->dst=%d, addr=0x%lx, length=%d",
		 __func__, blk->dst, (unsigned long int)blk->vaddr, blk->length);

	spin_lock_irqsave(&sb->list_lock, flags);
	if (list_empty(&sb->send_list)) {
		spin_unlock_irqrestore(&sb->list_lock, flags);
		pr_err("%s: send list is empty, send cnt = %d\n", __func__, sb->send_cnt);
		return 0;
	}
	sb->send_cnt--;
	/* remove from send list */
	list_del(&blk->list);
	/* add to pool list*/
	list_add_tail(&blk->list, &sb->pool_list);
	spin_unlock_irqrestore(&sb->list_lock, flags);
	dev_dbg(sb->dev, "%s: blk->dst=%d, sb->send_cnt=%d\n",
		 __func__, blk->dst, sb->send_cnt);
	return 0;
}

static struct block_node *slog_block_request(struct slog_bridge *sb,
					     u8 dst, u8 channel,
					     struct sblock *blk_rx, void *vaddr)
{
	struct block_node *blk;
	unsigned long flags;

	/* get until not enpty */
	while (list_empty(&sb->pool_list)) {
		dev_info(sb->dev, "pool empty!, send_cnt = %d",
			      sb->send_cnt);
		usleep_range(10, 20);
	}

	spin_lock_irqsave(&sb->list_lock, flags);
	/* get from pool list */
	blk = list_first_entry(&sb->pool_list, struct block_node, list);
	blk->send_len = 0;
	blk->vaddr = vaddr;
	blk->addr = blk_rx->addr;
	blk->length = blk_rx->length;
	blk->dst = dst;
	blk->channel = channel;
	sb->pending_cnt++;
	sb->total_size += blk_rx->length;
	dev_dbg(sb->dev, "%s: blk->dst=%d, addr=0x%lx, length=%d",
		 __func__, blk->dst, (unsigned long int)vaddr, blk_rx->length);

	/* remove from pool list */
	list_del(&blk->list);
	/* add to pending list*/
	list_add_tail(&blk->list, &sb->pending_list);
	spin_unlock_irqrestore(&sb->list_lock, flags);
	return blk;
}

static struct block_node *slog_block_send(struct slog_bridge *sb)
{
	struct block_node *blk;
	unsigned long flags;

	if (list_empty(&sb->pending_list)) {
		pr_debug("slog_bridge, %s: list empty, return\n", __func__);
		return NULL;
	}

	spin_lock_irqsave(&sb->list_lock, flags);
	/* get from pending list */
	blk = list_first_entry(&sb->pending_list, struct block_node, list);
	sb->pending_cnt--;
	sb->send_cnt++;

	/* remove from pending list */
	list_del(&blk->list);
	/* add to send list*/
	list_add_tail(&blk->list, &sb->send_list);
	spin_unlock_irqrestore(&sb->list_lock, flags);
	dev_dbg(sb->dev, "%s: blk->dst=%d, sb->pending_cnt=%d, sb->send_cnt=%d\n",
		 __func__, blk->dst, sb->pending_cnt, sb->send_cnt);
	return blk;
}

static void slog_usb_send_callback(char *buf, unsigned int len, void *p)
{
	struct slog_bridge *sb = (struct slog_bridge *)p;
	struct block_node *blk;

	list_for_each_entry(blk, &sb->send_list, list) {
		if (blk->vaddr == buf) {
			slog_block_release(sb, blk, len);
			break;
		}
	}
	sprd_add_action_ex(sb->log_action.actions,
			   SLOG_READ_DATA_ACTION,
			   (int)SLOG_POLL_ALL);
}

static void slog_bridge_event_callback(int event, void *data)
{
	struct slog_cb *scb = (struct slog_cb *)data;
	struct slog_bridge *sb  = scb->sb;
	struct sblock_mgr *sblock;

	sblock = scb->sblock;

	if (sb->wait_log_enable) {
		dev_dbg(sb->dev, "slog-%d-%d: wait_log_enable",
			sblock->dst, sblock->channel);
		return;
	}

	switch (event) {
	case SBLOCK_NOTIFY_OPEN:
		dev_dbg(sb->dev, "slog-%d-%d: SBLOCK_NOTIFY_READY",
			 sblock->dst, sblock->channel);
		if (!sblock_has_data(sblock, false))
			break;

		sprd_add_action_ex(sb->log_action.actions,
				   SLOG_READ_DATA_ACTION,
				   (int)scb->index);
		break;

	case SBLOCK_NOTIFY_RECV:
		dev_dbg(sb->dev, "slog-%d-%d: SBLOCK_NOTIFY_RECV",
			 sblock->dst, sblock->channel);

		sprd_add_action_ex(sb->log_action.actions,
				   SLOG_READ_DATA_ACTION,
				   (int)scb->index);

		break;

	case SBLOCK_NOTIFY_GET:
		sprd_add_action_ex(sb->log_action.actions,
				   SLOG_CAN_WRITE_ACTION, (int)SLOG_POLL_ALL);
		break;

	default:
		break;
	}
}

static void slog_bridge_register_work_fn(struct work_struct *work)
{
	u32 i;
	bool not_all_ok = false;
	struct slog_bridge *sb = container_of(work, struct slog_bridge,
						     register_work);

	if (!sb->tx_is_usb && !sb->sblock_tx) {
		sb->sblock_tx =
			sblock_register_notifier_ex(sb->dst_tx,
						    sb->ch_tx,
						    slog_bridge_event_callback,
						    &sb->log_cb_tx);
		if (sb->sblock_tx)
			sb->log_cb_tx.sblock = sb->sblock_tx;
		else
			not_all_ok = true;
	}

	for (i = 0; i < sb->rx_cnt; i++) {
		if (sb->sblock_rx[i])
			continue;

		sb->sblock_rx[i] =
			sblock_register_notifier_ex(sb->dst_rx[i],
						    sb->ch_rx[i],
						    slog_bridge_event_callback,
						    &sb->log_cb_rx[i]);
		if (sb->sblock_rx[i]) {
			sb->log_cb_rx[i].sblock = sb->sblock_rx[i];
			sb->rx_block_max_size = max(sb->rx_block_max_size,
						    sb->sblock_rx[i]->rxblksz);
			dev_info(sb->dev,
				 "sblock_rx[%d] rxblksz=%d\n",
				 i, sb->sblock_rx[i]->rxblksz);
		} else {
			not_all_ok = true;
		}
	}

	/* until all sblock register ok. */
	if (not_all_ok) {
		msleep(200);
		schedule_work(&sb->register_work);
	} else {
		for (i = 0; i < sb->rx_cnt; i++) {
			if (sb->sblock_rx[i])
				continue;

			if (sb->sblock_rx[i]->state == SBLOCK_STATE_READY
			    && sblock_has_data(sb->sblock_rx[i], false))
				sprd_add_action_ex(sb->log_action.actions,
						   SLOG_READ_DATA_ACTION,
						   (int)i);
		}

		dev_info(sb->dev, "all sblock register ok\n");
		if (sb->sblock_tx) {
			dev_info(sb->dev,
				 "rx_block_max_size=%d, txblksz=%d.\n",
				 sb->rx_block_max_size, sb->sblock_tx->txblksz);
			sb->rx_blks_cnt =
				sb->sblock_tx->txblksz / sb->rx_block_max_size;
		}
	}
}

static int slog_bridge_receive(struct slog_bridge *sb,
				     struct sblock_mgr *sblock_rx)
{
	struct sblock blk_rx;
	void *vaddr;

	/* all rx read data send to tx. */
	if (!(ushort volatile)log_transport || sblock_receive(sblock_rx->dst, sblock_rx->channel,
			      &blk_rx, 0) != 0)
		return -1;

	vaddr = __va(blk_rx.addr - sblock_rx->smem_virt +
		     sblock_rx->stored_smem_addr);
	slog_block_request(sb, sblock_rx->dst, sblock_rx->channel,
			   &blk_rx, vaddr);
	dev_dbg(sb->dev, "%s: sblock_rx->dst=%d, sb->pending_cnt=%d, sb->send_cnt=%d\n",
		 __func__, sblock_rx->dst, sb->pending_cnt, sb->send_cnt);

	return 0;
}

static void slog_bridge_write_to_block(struct slog_bridge *sb,
				       struct sblock_mgr *sblock_rx)
{
	u32 size, cnt;
	int ret;
	struct sblock blk_rx;
	struct sblock blk_tx;
	struct sblock_mgr *sblock_tx;

	if (!sb->sblock_tx) {
		dev_err(sb->dev, "slog sblock_tx is null!\n");
		return;
	}

	/* all rx read data send to tx. */
	while (sblock_receive(sblock_rx->dst,
			      sblock_rx->channel, &blk_rx, 0) == 0) {
		/* must sure slog tx block is bigger than rx block. */
		sblock_tx = sb->sblock_tx;
		if (sblock_get(sblock_tx->dst, sblock_tx->channel,
			       &blk_tx, -1)) {
			dev_err(sb->dev, "slog get tx failed!\n");
			break;
		}
		size = 0;
		cnt = 0;
		do {
			unalign_memcpy(blk_tx.addr + size,
				       blk_rx.addr, blk_rx.length);
			/* after copy release it. */
			if (sblock_release(sblock_rx->dst,
					   sblock_rx->channel, &blk_rx))
				dev_err(sb->dev, "dst = %d, failed to release block!\n",
					sblock_rx->dst);
			cnt++;
			size += blk_rx.length;
			if (cnt == sb->rx_blks_cnt)
				break;
		} while (sblock_receive(sblock_rx->dst,
					sblock_rx->channel, &blk_rx, 0) == 0);

		blk_tx.length = size;
		ret = sblock_send(sblock_tx->dst, sblock_tx->channel, &blk_tx);
		if (ret)
			dev_err(sb->dev,
				"sblock-%d-%d send failed, ret = %d!\n",
				sblock_tx->dst, sblock_tx->channel, ret);
	}
}

static void slog_bridge_write_to_usb(struct slog_bridge *sb)
{
	int ret;
	struct block_node *blk;

	/* all rx read data send to tx. */
	while (sb->send_cnt < SEND_CNT && (blk = slog_block_send(sb)) != NULL) {
		if (!(ushort volatile)log_transport) {
			dev_info(sb->dev, "%s: usb disconnected! left %d packets in pending list, %d in send list\n",
				 __func__, sb->pending_cnt, sb->send_cnt);
			slog_block_release(sb, blk, blk->length);
			continue;
		}

		/* vser_pass_user_write need pass __va(phy address) */
		ret = vser_pass_user_write(blk->vaddr, blk->length);
		if (ret < 0) {
			if (!(ushort volatile)log_transport)
				dev_info(sb->dev, "usb disconnected! ret=%d, dst=%d, sb->pending_cnt=%d, sb->send_cnt=%d\n",
					 ret, blk->dst, sb->pending_cnt, sb->send_cnt);
			else {
				sb->write_failed_cnt++;
				if (sb->write_failed_cnt == FAILED_CNT) {
					dev_info(sb->dev, "vser write fail, ret=%d, dst=%d, sb->pending_cnt=%d, sb->send_cnt=%d, failed_cnt=%d\n",
						 ret, blk->dst, sb->pending_cnt, sb->send_cnt, sb->write_failed_cnt);
					sb->write_failed_cnt = 0;
				}
			}
			slog_block_release(sb, blk, blk->length);
			break;
		}
	}
}

static void slog_bridge_data_move(struct slog_action *sba, int param)
{
	u32 i;
	struct sblock_mgr *sblock_rx;
	struct slog_bridge	*sb = sba->sb;
	int mask = 0;

	if (!(ushort volatile)log_transport) {
		sb->wait_log_enable = true;
		kernel_vser_set_pass_mode(false);

		/* wait until log_transport. */
		while (!(ushort volatile)log_transport) {
			dev_dbg(sb->dev, "wait log_transport=%d\n",
				log_transport);
			msleep(1000);
		}

		kernel_vser_set_pass_mode(true);
		/* poll all for enable log. */
		param = SLOG_POLL_ALL;
		sb->wait_log_enable = false;
	}

	if (sb->tx_is_usb)
		slog_bridge_write_to_usb(sb);

	/*  force poll all log channel. */
	param = SLOG_POLL_ALL;

	if (param < MAX_RX_BLOCK) {
		sblock_rx = sb->sblock_rx[param];
		if (!sblock_rx)
			return;

		if (sb->tx_is_usb)
			slog_bridge_receive(sb, sblock_rx);
		else
			slog_bridge_write_to_block(sb, sblock_rx);
	} else {
		/* poll all sblock rx. */
		for (i = 0; i < sb->rx_cnt; i++) {
			if (sb->sblock_rx[i])
				mask += 1<<i;
		}

		while (sb->pending_cnt < PENDING_CNT && mask > 0) {
			for (i = 0; i < sb->rx_cnt; i++) {
				if ((mask & (1<<i)) == 0)
					continue;

				if (sb->tx_is_usb) {
					if (slog_bridge_receive(sb, sb->sblock_rx[i]) < 0)
						mask &= (~BIT(i));
				} else
					slog_bridge_write_to_block(sb, sb->sblock_rx[i]);
			}
		}
	}

	if (sb->tx_is_usb)
		slog_bridge_write_to_usb(sb);
}

static void slog_bridge_do_actions(u32 action, int param, void *data)
{
	struct slog_action *sba = (struct slog_action *)data;

	switch (action) {
	case SLOG_READ_DATA_ACTION:
		slog_bridge_data_move(sba, param);
		break;

	case SLOG_CAN_WRITE_ACTION:
		slog_bridge_data_move(sba, param);
		break;
	default:
		break;
	}
}

static int slog_bridge_probe(struct platform_device *pdev)
{
	int ret, cnt, i;
	struct slog_bridge *sb;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	u32 data[MAX_RX_BLOCK];

	/* allcoc struct slog_bridge. */
	sb = devm_kzalloc(dev, sizeof(*sb), GFP_KERNEL);
	if (!sb)
		return -ENOMEM;

	sb->dev = dev;

	/* parse tx is usb. */
	ret = of_property_read_u32(np, "sprd,tx_is_usb", &data[0]);
	if (!ret) {
		sb->tx_is_usb = (data[0] > 0);
		dev_info(dev, "tx is usb = %d.\n", sb->tx_is_usb);
	}

	if (!sb->tx_is_usb) {
		/* parse tx_dst. */
		ret = of_property_read_u32(np, "sprd,tx_dst", &data[0]);
		if (ret) {
			dev_err(dev, "tx dst err =%d\n", ret);
			return ret;
		}
		sb->dst_tx = (u8)data[0];
		sb->ch_tx = SMSG_CH_PLOG;
	}

	/* parse  rx_dst */
	cnt = of_property_count_u32_elems(np, "sprd,rx_dst");
	if (cnt <= 0 || cnt > MAX_RX_BLOCK) {
		dev_err(dev, "rx_dst cnt err =%d\n", cnt);
		return cnt;
	}
	sb->rx_cnt = (u32)cnt;
	ret = of_property_read_u32_array(np, "sprd,rx_dst", data, sb->rx_cnt);
	if (ret) {
		dev_err(dev, "rx_dst err =%d\n", ret);
		return ret;
	}

	dev_info(dev, "probed, tx_is_usb=%d, rx_cnt=%d\n",
		sb->tx_is_usb, sb->rx_cnt);

	for (i = 0; i < cnt; i++) {
		sb->dst_rx[i] = (u8)data[i];
		sb->ch_rx[i] = SMSG_CH_PLOG;
		sb->log_cb_rx[i].sb = sb;
		sb->log_cb_rx[i].index = i;
		dev_dbg(dev, "sb->dst_rx[%d]=%d\n", i, sb->dst_rx[i]);
	}

	/* parse priority, default 90. */
	ret = of_property_read_u32(np, "sprd,priority", &sb->priority);
	if (ret)
		sb->priority = 90;

	/* create  log_action. */
	sprintf(sb->log_action.name, "slog-%d-%d", sb->dst_tx, sb->ch_tx);
	sb->log_action.actions =
		sprd_create_action_queue(sb->log_action.name,
					 slog_bridge_do_actions,
					 &sb->log_action, sb->priority);

	/* init action. */
	sb->log_action.sb = sb;
	sb->log_cb_tx.sb = sb;
	if (sb->tx_is_usb) {
		slog_block_list_init(sb);
		kernel_vser_register_callback(slog_usb_send_callback, sb);
	}

	INIT_WORK(&sb->register_work, slog_bridge_register_work_fn);
	spin_lock_init(&sb->list_lock);
	schedule_work(&sb->register_work);

	timer_setup(&sb->flush_timer,
			slog_bridge_flush_timer_fn,
			0);

	sb_record = sb;
	platform_set_drvdata(pdev, sb);

	return 0;
}

static int  slog_bridge_remove(struct platform_device *pdev)
{
	struct slog_bridge *sb = platform_get_drvdata(pdev);

	if (sb) {
		cancel_work_sync(&sb->register_work);

		if (sb->log_action.actions)
			sprd_destroy_action_queue(sb->log_action.actions);

		if (sb->tx_is_usb)
			slog_block_list_free(sb);

		del_timer(&sb->flush_timer);


		devm_kfree(&pdev->dev, sb);
		platform_set_drvdata(pdev, NULL);
	}

	return 0;
}

static const struct of_device_id slog_bridge_match_table[] = {
	{.compatible = "sprd,slog_bridge", },
	{ },
};

static struct platform_driver slog_bridge_driver = {
	.driver = {
		.name = "slog_bridge",
		.of_match_table = slog_bridge_match_table,
	},
	.probe = slog_bridge_probe,
	.remove = slog_bridge_remove,
};

static int __init slog_bridge_init(void)
{
	return platform_driver_register(&slog_bridge_driver);
}

static void __exit slog_bridge_exit(void)
{
	platform_driver_unregister(&slog_bridge_driver);
}

module_init(slog_bridge_init);
module_exit(slog_bridge_exit);

MODULE_AUTHOR("Zhou Wenping");
MODULE_DESCRIPTION("SIPC/slog_bridge driver");
MODULE_LICENSE("GPL");

