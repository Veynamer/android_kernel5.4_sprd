// SPDX-License-Identifier: GPL-2.0-only
/*
 * Unisoc UMS512 JPG driver
 *
 * Copyright (C) 2019 Unisoc, Inc.
 */

#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/sprd_iommu.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/dma-mapping.h>
#include <uapi/video/sprd_jpg.h>
#include "sprd_jpg_common.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "sprd-jpg: " fmt

static struct jpg_dev_t hw_dev;
static struct wakeup_source jpg_wakelock;

static char *jpg_clk_src[] = {
	"clk_src_76m8",
	"clk_src_96m",
	"clk_src_128m",
	"clk_src_153m6",
	"clk_src_192m",
	"clk_src_256m",
	"clk_src_307m2",
	"clk_src_384m",
	"clk_src_409m6",
	"clk_src_512m"
};
static char *syscon_name[] = {
	"aon-apb-eb-syscon",
	"reset-syscon"
};

static struct clock_name_map_t clock_name_map[ARRAY_SIZE(jpg_clk_src)];
static struct jpg_qos_cfg qos_cfg;
static struct register_gpr regs[ARRAY_SIZE(syscon_name)];

static irqreturn_t jpg_isr(int irq, void *data);
static irqreturn_t jpg_isr_thread(int irq, void *data);

static irqreturn_t jpg_isr(int irq, void *data)
{
	int int_status;

	struct jpg_fh *jpg_fp = hw_dev.jpg_fp;

	if (jpg_fp->is_clock_enabled == 0) {
		__pm_stay_awake(&jpg_wakelock);
		return IRQ_WAKE_THREAD;
	}

	int_status =
		readl_relaxed((void __iomem *)(hw_dev.sprd_jpg_virt +
			GLB_INT_RAW_OFFSET));
	if ((int_status) & 0xb) {
		if ((int_status >> 3) & 0x1) {
			/* JPEG ENC  MBIO DONE */
			writel_relaxed((1 << 3),
				(void __iomem *)(hw_dev.sprd_jpg_virt +
						GLB_INT_CLR_OFFSET));

			hw_dev.jpg_int_status |= 0x8;
			hw_dev.condition_work_MBIO = 1;
			wake_up_interruptible(&hw_dev.wait_queue_work_MBIO);
			dev_info(hw_dev.jpg_dev, "%s MBIO\n\n", __func__);
		}
		if ((int_status >> 0) & 0x1) {
			/* JPEG ENC BSM INIT */
			writel_relaxed((1 << 0),
				(void __iomem *)(hw_dev.sprd_jpg_virt +
						GLB_INT_CLR_OFFSET));
			hw_dev.jpg_int_status |= 0x1;

			hw_dev.condition_work_BSM = 1;
			wake_up_interruptible(&hw_dev.wait_queue_work_BSM);
			dev_err(hw_dev.jpg_dev, "%s BSM\n", __func__);
		}
		if ((int_status >> 1) & 0x1) {
			/* JPEG ENC VLC DONE INIT */
			writel_relaxed((1 << 1),
				(void __iomem *)(hw_dev.sprd_jpg_virt +
						GLB_INT_CLR_OFFSET));
			hw_dev.jpg_int_status |= 0x2;

			hw_dev.condition_work_VLC = 1;
			wake_up_interruptible(&hw_dev.wait_queue_work_VLC);
			dev_dbg(hw_dev.jpg_dev, "%s VLC\n", __func__);
		}

	}

	if (hw_dev.version >= SHARKL5PRO) {
		int_status =
			readl_relaxed((void __iomem *)(hw_dev.sprd_jpg_virt +
				IOMMU_INT_RAW_OFFSET));
		if (int_status) {
			writel_relaxed((int_status),
				(void __iomem *)(hw_dev.sprd_jpg_virt +
				IOMMU_INT_CLR_OFFSET));
		}
		dev_info(hw_dev.jpg_dev, "%s iommu status 0x%x\n", __func__, int_status);
	}

	return IRQ_HANDLED;
}

static irqreturn_t jpg_isr_thread(int irq, void *data)
{
	int ret;
	int int_status;

	ret = jpg_clk_enable(&hw_dev);
	if (ret == 0) {
		int_status =
			readl_relaxed((void __iomem *)(hw_dev.sprd_jpg_virt +
				GLB_INT_RAW_OFFSET));
		if (int_status) {
			writel_relaxed((int_status),
				(void __iomem *)(hw_dev.sprd_jpg_virt +
				GLB_INT_CLR_OFFSET));
		}
		if (hw_dev.version >= SHARKL5PRO) {
			int_status =
				readl_relaxed((void __iomem *)(hw_dev.sprd_jpg_virt +
					IOMMU_INT_RAW_OFFSET));
			dev_info(hw_dev.jpg_dev, "jpg_iommu_int raw 0x%x\n", int_status);
			if (int_status) {
				writel_relaxed((int_status),
					(void __iomem *)(hw_dev.sprd_jpg_virt +
					IOMMU_INT_CLR_OFFSET));
			}
		}
		jpg_clk_disable(&hw_dev);
	}
	__pm_relax(&jpg_wakelock);
	return IRQ_HANDLED;
}

static const struct sprd_jpg_cfg_data sharkle_jpg_data = {
	.version = SHARKLE,
	.max_freq_level = 4,
};

static const struct sprd_jpg_cfg_data pike2_jpg_data = {
	.version = PIKE2,
	.max_freq_level = 4,
};

static const struct sprd_jpg_cfg_data sharkl3_jpg_data = {
	.version = SHARKL3,
	.max_freq_level = 4,

};
static const struct sprd_jpg_cfg_data sharkl5_jpg_data = {
	.version = SHARKL5,
	.max_freq_level = 4,
	.qos_reg_offset = 0x30
};

static const struct sprd_jpg_cfg_data roc1_jpg_data = {
	.version = ROC1,
	.max_freq_level = 4,
	.qos_reg_offset = 0x30
};

static const struct sprd_jpg_cfg_data sharkl5pro_jpg_data = {
	.version = SHARKL5PRO,
	.max_freq_level = 4,
	//.qos_reg_offset = 0x30
};

static const struct sprd_jpg_cfg_data qogirl6_jpg_data = {
	.version = QOGIRL6,
	.max_freq_level = 4,
	.qos_reg_offset = 0x30
};

static const struct sprd_jpg_cfg_data qogirn6pro_jpg_data = {
	.version = QOGIRN6PRO,
	.max_freq_level = 5,
	.qos_reg_offset = 0x30
};

static const struct of_device_id of_match_table_jpg[] = {

	{.compatible = "sprd,sharkle-jpg", .data = &sharkle_jpg_data},
	{.compatible = "sprd,pike2-jpg", .data = &pike2_jpg_data},
	{.compatible = "sprd,sharkl3-jpg", .data = &sharkl3_jpg_data},
	{.compatible = "sprd,sharkl5-jpg", .data = &sharkl5_jpg_data},
	{.compatible = "sprd,roc1-jpg", .data = &roc1_jpg_data},
	{.compatible = "sprd,sharkl5pro-jpg", .data = &sharkl5pro_jpg_data},
	{.compatible = "sprd,qogirl6-jpg", .data = &qogirl6_jpg_data},
	{.compatible = "sprd,qogirn6pro-jpg", .data = &qogirn6pro_jpg_data},
	{},
};

static int jpg_parse_dt(struct platform_device *pdev)
{
	struct device *dev = &(pdev->dev);
	struct device_node *np = dev->of_node;
	struct resource *res;
	int ret, i, j = 0;
	struct device_node *qos_np = NULL;
	char *pname;
	struct regmap *tregmap;
	uint32_t syscon_args[2];

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "can't get IORESOURCE_MEM JPG\n");
		return -EINVAL;
	}

	hw_dev.sprd_jpg_phys = res->start;
	hw_dev.sprd_jpg_virt =
		(unsigned long)devm_ioremap_resource(&pdev->dev, res);
	if (!hw_dev.sprd_jpg_virt)
		return -ENOMEM;

	hw_dev.irq = platform_get_irq(pdev, 0);
	hw_dev.dev_np = np;
	hw_dev.jpg_dev = dev;

	dev_info(dev, "sprd_jpg_phys jpg:  0X%lx\n", hw_dev.sprd_jpg_phys);
	dev_info(dev, "sprd_jpg_virt jpg:  0x%lx\n", hw_dev.sprd_jpg_virt);
	dev_info(dev, " hw_dev.irq  0X%x\n", hw_dev.irq);

	for (i = 0; i < ARRAY_SIZE(syscon_name); i++) {
		pname = syscon_name[i];
		tregmap = syscon_regmap_lookup_by_phandle_args(np, pname, 2, syscon_args);
		if (IS_ERR(tregmap)) {
			dev_err(dev, "Read JPG Dts %s regmap fail\n",
				pname);
			regs[i].gpr = NULL;
			regs[i].reg = 0x0;
			regs[i].mask = 0x0;
			continue;
		}

		regs[i].gpr = tregmap;
		regs[i].reg = syscon_args[0];
		regs[i].mask = syscon_args[1];
		dev_info(dev, "JPG syscon[%s]%p, offset 0x%x, mask 0x%x\n",
			pname, regs[i].gpr, regs[i].reg, regs[i].mask);
	}


	for (i = 0; i < ARRAY_SIZE(jpg_clk_src); i++) {
		struct clk *clk_parent;
		unsigned long frequency;

		clk_parent = of_clk_get_by_name(np, jpg_clk_src[i]);
		if (IS_ERR_OR_NULL(clk_parent)) {
			dev_info(dev, "clk %s not found,continue to find next clock\n",
				jpg_clk_src[i]);
			continue;
		}
		frequency = clk_get_rate(clk_parent);

		clock_name_map[j].name = jpg_clk_src[i];
		clock_name_map[j].freq = frequency;
		clock_name_map[j].clk_parent = clk_parent;

		dev_info(dev, "jpg clk in dts file: clk[%d] = (%ld, %s)\n", j,
			frequency, clock_name_map[j].name);
		j++;
	}
	hw_dev.clock_name_map = clock_name_map;

	qos_np = of_parse_phandle(np, "jpg_qos", 0);
	if (!qos_np) {
		dev_warn(dev, "can't find jpg qos cfg node\n");
		hw_dev.jpg_qos_exist_flag = 0;
	} else {
		ret = of_property_read_u8(qos_np, "awqos",
						&qos_cfg.awqos);
		if (ret)
			dev_warn(dev, "read awqos_low failed, use default\n");

		ret = of_property_read_u8(qos_np, "arqos-low",
						&qos_cfg.arqos_low);
		if (ret)
			dev_warn(dev, "read arqos-low failed, use default\n");

		ret = of_property_read_u8(qos_np, "arqos-high",
						&qos_cfg.arqos_high);
		if (ret)
			dev_warn(dev, "read arqos-high failed, use default\n");

		hw_dev.jpg_qos_exist_flag = 1;
	}
	dev_info(dev, "%x, %x, %x, %x", qos_cfg.awqos, qos_cfg.arqos_high,
		qos_cfg.arqos_low, qos_cfg.reg_offset);

	return 0;
}
static long jpg_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	int cmd0;
	struct jpg_iommu_map_data mapdata;
	struct jpg_iommu_map_data ummapdata;
	struct clk *clk_parent;
	unsigned long frequency;
	struct jpg_fh *jpg_fp = filp->private_data;

	if (!jpg_fp) {
		dev_err(hw_dev.jpg_dev, "%s,%d jpg_fp NULL !\n", __func__, __LINE__);
		return -EINVAL;
	}

	switch (cmd) {
	case JPG_CONFIG_FREQ:
		get_user(hw_dev.freq_div, (int __user *)arg);
		clk_parent = jpg_get_clk_src_name(hw_dev.clock_name_map,
			hw_dev.freq_div, hw_dev.max_freq_level);

		hw_dev.jpg_parent_clk = clk_parent;

		dev_dbg(hw_dev.jpg_dev, "%s,%d clk_set_parent() !\n", __func__, __LINE__);
		dev_info(hw_dev.jpg_dev, "JPG_CONFIG_FREQ %d\n", hw_dev.freq_div);
		break;

	case JPG_GET_FREQ:
		frequency = clk_get_rate(hw_dev.jpg_clk);
		ret = jpg_find_freq_level(hw_dev.clock_name_map,
			frequency, hw_dev.max_freq_level);
		put_user(ret, (int __user *)arg);
		dev_info(hw_dev.jpg_dev, "jpg ioctl JPG_GET_FREQ %d\n", ret);
		break;

	case JPG_ENABLE:
		dev_dbg(hw_dev.jpg_dev, "jpg ioctl JPG_ENABLE\n");
		ret = jpg_clk_enable(&hw_dev);
		if (ret == 0)
			jpg_fp->is_clock_enabled = 1;

		break;
	case JPG_DISABLE:
		if (jpg_fp->is_clock_enabled == 1)
			jpg_clk_disable(&hw_dev);

		jpg_fp->is_clock_enabled = 0;
		dev_info(hw_dev.jpg_dev, "jpg ioctl JPG_DISABLE\n");
		break;
	case JPG_ACQUAIRE:
		ret = down_timeout(&hw_dev.jpg_mutex,
				   msecs_to_jiffies(JPG_TIMEOUT_MS));
		if (ret) {
			dev_err(hw_dev.jpg_dev, "jpg error timeout\n");
			return ret;
		}

		jpg_fp->is_jpg_acquired = 1;
		hw_dev.jpg_fp = jpg_fp;
		break;
	case JPG_RELEASE:
		dev_dbg(hw_dev.jpg_dev, "jpg ioctl JPG_RELEASE\n");
		if (jpg_fp->is_jpg_acquired == 1) {
			jpg_fp->is_jpg_acquired = 0;
			up(&hw_dev.jpg_mutex);
		}
		break;

	case JPG_START:
		dev_dbg(hw_dev.jpg_dev, "jpg ioctl JPG_START\n");
		ret =
		    wait_event_interruptible_timeout
		    (hw_dev.wait_queue_work_VLC,
		     hw_dev.condition_work_VLC,
		     msecs_to_jiffies(JPG_TIMEOUT_MS));
		if (ret == -ERESTARTSYS) {
			dev_err(hw_dev.jpg_dev, "jpg error start -ERESTARTSYS\n");
			hw_dev.jpg_int_status |= 1 << 30;
			ret = -EINVAL;
		} else if (ret == 0) {
			dev_err(hw_dev.jpg_dev, "jpg error start  timeout\n");
			hw_dev.jpg_int_status |= 1 << 31;
			ret = -ETIMEDOUT;
		} else {
			ret = 0;
		}
		if (ret) {
			/* clear jpg int */
			writel_relaxed((1 << 3) | (1 << 2) | (1 << 1) |
				       (1 << 0),
				(void __iomem *)(hw_dev.sprd_jpg_virt +
						GLB_INT_CLR_OFFSET));
		}
		put_user(hw_dev.jpg_int_status, (int __user *)arg);
		hw_dev.condition_work_MBIO = 0;
		hw_dev.condition_work_VLC = 0;
		hw_dev.condition_work_BSM = 0;
		hw_dev.jpg_int_status = 0;
		dev_dbg(hw_dev.jpg_dev, "jpg ioctl JPG_START end\n");
		break;

	case JPG_RESET:
		dev_dbg(hw_dev.jpg_dev, "jpg ioctl JPG_RESET\n");
		regmap_update_bits(regs[RESET].gpr, regs[RESET].reg,
				   regs[RESET].mask, regs[RESET].mask);
		regmap_update_bits(regs[RESET].gpr, regs[RESET].reg,
				   regs[RESET].mask, 0);
		if (hw_dev.jpg_qos_exist_flag)
			writel_relaxed((qos_cfg.arqos_high << 8)
				| (qos_cfg.arqos_low << 4)
				| qos_cfg.awqos,
				((void __iomem *)hw_dev.sprd_jpg_virt
				+ qos_cfg.reg_offset));

		break;

	case JPG_ACQUAIRE_MBIO_VLC_DONE:
		cmd0 = (int)arg;
		ret = jpg_poll_mbio_vlc_done(&hw_dev, cmd0);
		break;

	case JPG_GET_IOMMU_STATUS:
		ret = sprd_iommu_attach_device(hw_dev.jpg_dev);

		break;

	case JPG_GET_IOVA:

		ret =
		    copy_from_user((void *)&mapdata,
				   (const void __user *)arg,
				   sizeof(struct jpg_iommu_map_data));
		if (ret) {
			dev_err(hw_dev.jpg_dev, "copy mapdata failed, ret %d\n", ret);
			return -EFAULT;
		}

		ret = jpg_get_iova(&hw_dev, &mapdata, (void __user *)arg);
		dev_info(hw_dev.jpg_dev, "JPG_GET_IOVA end\n");
		break;

	case JPG_FREE_IOVA:

		ret =
		    copy_from_user((void *)&ummapdata,
				   (const void __user *)arg,
				   sizeof(struct jpg_iommu_map_data));
		if (ret) {
			dev_err(hw_dev.jpg_dev, "copy ummapdata failed, ret %d\n", ret);
			return -EFAULT;
		}

		ret = jpg_free_iova(&hw_dev, &ummapdata);
		dev_info(hw_dev.jpg_dev, "JPG_FREE_IOVA end\n");
		break;

	case JPG_VERSION:

		dev_dbg(hw_dev.jpg_dev, "jpg version -enter\n");
		put_user(hw_dev.version, (int __user *)arg);

		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int jpg_nocache_mmap(struct file *filp, struct vm_area_struct *vma)
{
	size_t memsize = vma->vm_end - vma->vm_start;

	if (memsize > SPRD_JPG_MAP_SIZE) {
		pr_err("%s, need:%lx should be:%x ", __func__, memsize, SPRD_JPG_MAP_SIZE);
		return -EINVAL;
	}
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_pgoff = (hw_dev.sprd_jpg_phys >> PAGE_SHIFT);
	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    memsize, vma->vm_page_prot)) {
		dev_err(hw_dev.jpg_dev, "%s failed\n", __func__);
		return -EAGAIN;
	}
	dev_info(hw_dev.jpg_dev, "mmap %x,%x,%x,%lx\n", (unsigned int)PAGE_SHIFT,
		(unsigned int)vma->vm_start,
		(unsigned int)(memsize),
		hw_dev.sprd_jpg_phys);
	return 0;
}

static int jpg_open(struct inode *inode, struct file *filp)
{
	int ret;
	struct jpg_fh *jpg_fp = kmalloc(sizeof(struct jpg_fh), GFP_KERNEL);

	filp->private_data = jpg_fp;
	jpg_fp->is_clock_enabled = 0;
	jpg_fp->is_jpg_acquired = 0;
	jpg_fp->hw_dev = &hw_dev;

	hw_dev.condition_work_MBIO = 0;
	hw_dev.condition_work_VLC = 0;
	hw_dev.condition_work_BSM = 0;
	hw_dev.jpg_int_status = 0;

	pm_runtime_get_sync(hw_dev.jpg_dev);
	ret = 0;
	dev_info(hw_dev.jpg_dev, "jpg pw_on: ret %d", ret);


	dev_info(hw_dev.jpg_dev, "%s ret %d jpg_fp %p\n", __func__, ret, jpg_fp);

	return ret;
}

static int jpg_release(struct inode *inode, struct file *filp)
{
	struct jpg_fh *jpg_fp = filp->private_data;
	int ret = 0;

	if (!jpg_fp) {
		dev_err(hw_dev.jpg_dev, "%s,%d jpg_fp NULL !\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (jpg_fp->is_clock_enabled) {
		dev_err(hw_dev.jpg_dev, "error occurred and close clock\n");
		jpg_clk_disable(&hw_dev);
		jpg_fp->is_clock_enabled = 0;
	}

	if (jpg_fp->is_jpg_acquired) {
		dev_err(hw_dev.jpg_dev, "error occurred and up jpg_mutex\n");
		up(&hw_dev.jpg_mutex);
	}

	pm_runtime_mark_last_busy(hw_dev.jpg_dev);
	pm_runtime_put_sync(hw_dev.jpg_dev);
	dev_info(hw_dev.jpg_dev, "jpg pw_off: ret %d", ret);

	dev_info(hw_dev.jpg_dev, "%s %p\n", __func__, jpg_fp);
	kfree(filp->private_data);

	return 0;
}

static const struct file_operations jpg_fops = {
	.owner = THIS_MODULE,
	.mmap = jpg_nocache_mmap,
	.open = jpg_open,
	.release = jpg_release,
	.unlocked_ioctl = jpg_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_jpg_ioctl,
#endif
};

static struct miscdevice jpg_dev = {
	.minor = JPG_MINOR,
	.name = "sprd_jpg",
	.fops = &jpg_fops,
};

static int jpg_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	const struct of_device_id *of_id;
	struct device_node *node = pdev->dev.of_node;

	of_id = of_match_node(of_match_table_jpg, node);
	if (of_id)
		hw_dev.jpg_cfg_data =
		    (struct sprd_jpg_cfg_data *)of_id->data;
	else {
		dev_err(dev, "failed to find matched id for sprd_jpg\n");
		return -ENODEV;
	}
	if (jpg_parse_dt(pdev)) {
		dev_err(dev, "jpg_parse_dt failed\n");
		return -EINVAL;
	}

	hw_dev.version = hw_dev.jpg_cfg_data->version;
	hw_dev.max_freq_level = hw_dev.jpg_cfg_data->max_freq_level;
	qos_cfg.reg_offset =
	    hw_dev.jpg_cfg_data->qos_reg_offset;

	sema_init(&hw_dev.jpg_mutex, 1);

	init_waitqueue_head(&hw_dev.wait_queue_work_MBIO);
	hw_dev.condition_work_MBIO = 0;
	init_waitqueue_head(&hw_dev.wait_queue_work_VLC);
	hw_dev.condition_work_VLC = 0;
	init_waitqueue_head(&hw_dev.wait_queue_work_BSM);
	hw_dev.condition_work_BSM = 0;
	hw_dev.jpg_int_status = 0;

	hw_dev.freq_div = DEFAULT_FREQ_DIV;

	hw_dev.jpg_clk = NULL;
	hw_dev.jpg_parent_clk = NULL;
	hw_dev.jpg_domain_eb = NULL;
	hw_dev.clk_vsp_mq_ahb_eb = NULL;
	hw_dev.jpg_dev_eb = NULL;
	hw_dev.jpg_ckg_eb = NULL;

	hw_dev.jpg_fp = NULL;

	INIT_LIST_HEAD(&hw_dev.map_list);

	ret = jpg_get_mm_clk(&hw_dev);
	ret = 0;

	ret = misc_register(&jpg_dev);
	if (ret) {
		dev_err(dev, "cannot register miscdev on minor=%d (%d)\n",
		       JPG_MINOR, ret);
		goto errout;
	}

	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64))) {
		if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32)))
			dev_err(dev, "jpg: failed to set dma mask!\n");
	} else {
		dev_info(dev, "jpg: set dma mask as 64bit\n");
	}

	/* register isr */
	ret =
		devm_request_threaded_irq(&pdev->dev, hw_dev.irq, jpg_isr,
			jpg_isr_thread, 0, "JPG", &hw_dev);
	if (ret) {
		dev_err(dev, "jpg: failed to request irq!\n");
		ret = -EINVAL;
		goto errout2;
	}
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	dev_err(dev, "sprd_jpg probe with genpd\n");

	return 0;

errout2:
	misc_deregister(&jpg_dev);

errout:
	return ret;
}

static int jpg_remove(struct platform_device *pdev)
{
	misc_deregister(&jpg_dev);

	free_irq(hw_dev.irq, &hw_dev);

	if (hw_dev.jpg_parent_clk)
		clk_put(hw_dev.jpg_parent_clk);
	if (hw_dev.jpg_clk)
		clk_put(hw_dev.jpg_clk);
	if (hw_dev.jpg_ckg_eb)
		clk_put(hw_dev.jpg_ckg_eb);
	if (hw_dev.jpg_dev_eb)
		clk_put(hw_dev.jpg_dev_eb);
	if (hw_dev.jpg_domain_eb)
		clk_put(hw_dev.jpg_domain_eb);
	if (hw_dev.clk_vsp_mq_ahb_eb)
		clk_put(hw_dev.clk_vsp_mq_ahb_eb);

	return 0;
}

static struct platform_driver jpg_driver = {
	.probe = jpg_probe,
	.remove = jpg_remove,
	.driver = {
		   .name = "sprd-jpg",
		   .of_match_table = of_match_ptr(of_match_table_jpg),
		   },
};
module_platform_driver(jpg_driver);

MODULE_DESCRIPTION("SPRD JPG Driver");
MODULE_LICENSE("GPL v2");

