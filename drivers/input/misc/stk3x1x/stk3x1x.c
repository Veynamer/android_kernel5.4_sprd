/*
 *  stk3x1x.c - Linux kernel modules for sensortek stk301x, stk321x, stk331x 
 *  , and stk3410 proximity/ambient light sensor
 *
 *  Copyright (C) 2012~2013 Lex Hsieh / sensortek <lex_hsieh@sensortek.com.tw>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/sysctl.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
	 
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/fs.h>
#include <asm/uaccess.h>

#include <linux/miscdevice.h>
	 
//#include <linux/wakelock.h>

#define DRIVER_VERSION  "3.7.2"

/* Driver Settings */
#define CONFIG_STK_PS_ALS_USE_CHANGE_THRESHOLD
#ifdef CONFIG_STK_PS_ALS_USE_CHANGE_THRESHOLD
#define STK_ALS_CHANGE_THD      10      /* The threshold to trigger ALS interrupt, unit: lux */
#endif  /* #ifdef CONFIG_STK_PS_ALS_USE_CHANGE_THRESHOLD */
#define STK_INT_PS_MODE                 1       /* 1, 2, or 3   */
//#define STK_POLL_PS
#define STK_POLL_ALS            /* ALS interrupt is valid only when STK_INT_PS_MODE = 1 or 4*/
#define STK_TUNE0
#define CALI_PS_EVERY_TIME
#define STK_DEBUG_PRINTF
#define SPREADTRUM_PLATFORM
#define STK_ALS_FIR
#define STK_IRS
//#define STK_CHK_REG
//#define STK_GES
//#define STK_QUALCOMM_POWER_CTRL
//#define QUALCOMM_PLATFORM

#ifdef SPREADTRUM_PLATFORM
#include "stk3x1x.h"
#else
        #include "stk3x1x.h"
#endif

//#define CONFIG_CALIBRATION
#define PS_VALUE_LOG

/* Define Register Map */
#define STK_STATE_REG                   0x00
#define STK_PSCTRL_REG                  0x01
#define STK_ALSCTRL_REG                         0x02
#define STK_LEDCTRL_REG                         0x03
#define STK_INT_REG                             0x04
#define STK_WAIT_REG                    0x05
#define STK_THDH1_PS_REG                0x06
#define STK_THDH2_PS_REG                0x07
#define STK_THDL1_PS_REG                0x08
#define STK_THDL2_PS_REG                0x09
#define STK_THDH1_ALS_REG               0x0A
#define STK_THDH2_ALS_REG               0x0B
#define STK_THDL1_ALS_REG               0x0C
#define STK_THDL2_ALS_REG               0x0D
#define STK_FLAG_REG                    0x10
#define STK_DATA1_PS_REG                0x11
#define STK_DATA2_PS_REG                0x12
#define STK_DATA1_ALS_REG               0x13
#define STK_DATA2_ALS_REG               0x14
#define STK_DATA1_OFFSET_REG    0x15
#define STK_DATA2_OFFSET_REG    0x16
#define STK_DATA1_IR_REG                0x17
#define STK_DATA2_IR_REG                0x18
#define STK_PDT_ID_REG                  0x3E
#define STK_RSRVD_REG                   0x3F
#define STK_SW_RESET_REG                0x80

#define STK_GSCTRL_REG                  0x1A
#define STK_FLAG2_REG                   0x1C

/* Define state reg */
#define STK_STATE_EN_IRS_SHIFT          7
#define STK_STATE_EN_AK_SHIFT   6
#define STK_STATE_EN_ASO_SHIFT          5
#define STK_STATE_EN_IRO_SHIFT          4
#define STK_STATE_EN_WAIT_SHIFT         2
#define STK_STATE_EN_ALS_SHIFT          1
#define STK_STATE_EN_PS_SHIFT   0

#define STK_STATE_EN_IRS_MASK   0x80
#define STK_STATE_EN_AK_MASK    0x40
#define STK_STATE_EN_ASO_MASK   0x20
#define STK_STATE_EN_IRO_MASK   0x10
#define STK_STATE_EN_WAIT_MASK  0x04
#define STK_STATE_EN_ALS_MASK   0x02
#define STK_STATE_EN_PS_MASK    0x01

/* Define PS ctrl reg */
#define STK_PS_PRS_SHIFT                6
#define STK_PS_GAIN_SHIFT               4
#define STK_PS_IT_SHIFT                         0

#define STK_PS_PRS_MASK                 0xC0
#define STK_PS_GAIN_MASK                        0x30
#define STK_PS_IT_MASK                  0x0F

/* Define ALS ctrl reg */
#define STK_ALS_PRS_SHIFT               6
#define STK_ALS_GAIN_SHIFT              4
#define STK_ALS_IT_SHIFT                        0

#define STK_ALS_PRS_MASK                0xC0
#define STK_ALS_GAIN_MASK               0x30
#define STK_ALS_IT_MASK                 0x0F

/* Define LED ctrl reg */
#define STK_LED_IRDR_SHIFT              6
#define STK_LED_DT_SHIFT                0

#define STK_LED_IRDR_MASK               0xC0
#define STK_LED_DT_MASK                 0x3F

/* Define interrupt reg */
#define STK_INT_CTRL_SHIFT              7
#define STK_INT_OUI_SHIFT               4
#define STK_INT_ALS_SHIFT               3
#define STK_INT_PS_SHIFT                        0

#define STK_INT_CTRL_MASK               0x80
#define STK_INT_OUI_MASK                        0x10
#define STK_INT_ALS_MASK                        0x08
#define STK_INT_PS_MASK                 0x07

#define STK_INT_ALS                             0x08

/* Define flag reg */
#define STK_FLG_ALSDR_SHIFT             7
#define STK_FLG_PSDR_SHIFT              6
#define STK_FLG_ALSINT_SHIFT            5
#define STK_FLG_PSINT_SHIFT             4
#define STK_FLG_OUI_SHIFT               2
#define STK_FLG_IR_RDY_SHIFT            1
#define STK_FLG_NF_SHIFT                0

#define STK_FLG_ALSDR_MASK              0x80
#define STK_FLG_PSDR_MASK               0x40
#define STK_FLG_ALSINT_MASK             0x20
#define STK_FLG_PSINT_MASK              0x10
#define STK_FLG_OUI_MASK                        0x04
#define STK_FLG_IR_RDY_MASK             0x02
#define STK_FLG_NF_MASK                 0x01

/* Define flag2 reg */
#define STK_FLG2_INT_GS_SHIFT           6
#define STK_FLG2_GS10_SHIFT             5
#define STK_FLG2_GS01_SHIFT             4

#define STK_FLG2_INT_GS_MASK    0x40
#define STK_FLG2_GS10_MASK              0x20
#define STK_FLG2_GS01_MASK              0x10


/* misc define */
#define MIN_ALS_POLL_DELAY_NS   110000000


#ifdef STK_TUNE0
        #define STK_MAX_MIN_DIFF        120	//100
        #define STK_LT_N_CT     40
        #define STK_HT_N_CT     80	//50
#endif  /* #ifdef STK_TUNE0 */

#define STK_IRC_MAX_ALS_CODE            20000
#define STK_IRC_MIN_ALS_CODE            25
#define STK_IRC_MIN_IR_CODE             50
#define STK_IRC_ALS_DENOMI              2
#define STK_IRC_ALS_NUMERA              5
#define STK_IRC_ALS_CORREC              850

#define DEVICE_NAME             "stk_ps"
#define ALS_NAME "lightsensor"
#define PS_NAME "proximity"

#ifdef STK_QUALCOMM_POWER_CTRL
        /* POWER SUPPLY VOLTAGE RANGE */
        #define STK3X1X_VDD_MIN_UV      2000000
        #define STK3X1X_VDD_MAX_UV      3300000
        #define STK3X1X_VIO_MIN_UV              1750000
        #define STK3X1X_VIO_MAX_UV      1950000
#endif

#ifdef SPREADTRUM_PLATFORM
extern int sprd_3rdparty_gpio_pls_irq;

static struct stk3x1x_platform_data stk3x1x_pfdata={ 
  .state_reg = 0x0,    /* disable all */ 
  .psctrl_reg = 0x32,    /* ps_persistance=1, ps_gain=64X, PS_IT=0.391ms */ 
  .alsctrl_reg = 0x39,  /* als_persistance=1, als_gain=64X, ALS_IT=50ms */
  .ledctrl_reg = 0xFF,   /* 100mA IRDR, 64/64 LED duty */ 
  .wait_reg = 0x07,    /* 50 ms */   
  .ps_thd_h =1700, 
  .ps_thd_l = 1500, 
  .int_pin = 52,//216,//GPIO_PROX_INT//sprd_3rdparty_gpio_pls_irq
  .transmittance = 500, 
}; 
#endif

#ifdef STK_ALS_FIR
#define STK_FIR_LEN     8	//32 //8
#define MAX_FIR_LEN 32
struct data_filter {
    u16 raw[MAX_FIR_LEN];
    int sum;
    int number;
    int idx;
};
#endif

#ifdef STK_GES
union stk_ges_operation{
        uint8_t ops[4];
        struct {
                uint8_t rw_len_retry;
                uint8_t reg;
                uint8_t reg_value_retry_crit;
                uint8_t sleep_10ns;
        }action;
};

union stk_ges_operation stk_ges_op[10] =
{
        {.ops={0xc1, 0x24, 0, 0}},
        {.ops={0, 0, 0, 0}},
        {.ops={0, 0, 0, 0}},
        {.ops={0, 0, 0, 0}},
        {.ops={0, 0, 0, 0}},
        {.ops={0, 0, 0, 0}},
        {.ops={0, 0, 0, 0}},
        {.ops={0, 0, 0, 0}},
        {.ops={0, 0, 0, 0}},
        {.ops={0, 0, 0, 0}}
};
#endif

struct stk3x1x_data {
        struct i2c_client *client;
        struct stk3x1x_platform_data *pdata;
#if (!defined(STK_POLL_PS) || !defined(STK_POLL_ALS))
    int32_t irq;
    struct work_struct stk_work;
        struct workqueue_struct *stk_wq;
#endif
        uint16_t ir_code;
        uint16_t als_correct_factor;
        uint8_t alsctrl_reg;
        uint8_t psctrl_reg;
        uint8_t ledctrl_reg;
        uint8_t state_reg;
        int             int_pin;
        uint8_t wait_reg;
        uint8_t int_reg;
#ifdef CONFIG_HAS_EARLYSUSPEND
        //struct early_suspend stk_early_suspend;
#endif
        uint16_t ps_thd_h;
        uint16_t ps_thd_l;
#ifdef CALI_PS_EVERY_TIME
        uint16_t ps_high_thd_boot;
        uint16_t ps_low_thd_boot;
#endif
        struct mutex io_lock;
        struct input_dev *ps_input_dev;
        int32_t ps_distance_last;
        bool ps_enabled;
        bool re_enable_ps;
       // struct wake_lock ps_wakelock;
#ifdef STK_POLL_PS
        struct hrtimer ps_timer;
    	struct work_struct stk_ps_work;
        struct workqueue_struct *stk_ps_wq;
        struct wake_lock ps_nosuspend_wl;
#endif
#ifdef PS_VALUE_LOG
        ktime_t ps_log_poll_delay;
        struct hrtimer ps_log_timer;
    	struct work_struct stk_ps_log_work;
        struct workqueue_struct *stk_ps_log_wq;
#endif
        struct input_dev *als_input_dev;
        int32_t als_lux_last;
        uint32_t als_transmittance;
        bool als_enabled;
        bool re_enable_als;
        ktime_t ps_poll_delay;
        ktime_t als_poll_delay;
#ifdef STK_POLL_ALS
    struct work_struct stk_als_work;
        struct hrtimer als_timer;
        struct workqueue_struct *stk_als_wq;
#endif
        bool first_boot;
#ifdef STK_TUNE0
        uint16_t psa;
        uint16_t psi;
        uint16_t psi_set;
        struct hrtimer ps_tune0_timer;
        struct workqueue_struct *stk_ps_tune0_wq;
    struct work_struct stk_ps_tune0_work;
        ktime_t ps_tune0_delay;
        bool tune_zero_init_proc;
        uint32_t ps_stat_data[3];
        int data_count;
        int stk_max_min_diff;
        int stk_lt_n_ct;
        int stk_ht_n_ct;
#endif
#ifdef STK_ALS_FIR
        struct data_filter      fir;
        atomic_t                firlength;
#endif
        atomic_t        recv_reg;

#ifdef STK_GES
        struct input_dev *ges_input_dev;
        int ges_enabled;
        int re_enable_ges;
        atomic_t gesture2;
#endif
#ifdef STK_IRS
        int als_data_index;
#endif
#ifdef STK_QUALCOMM_POWER_CTRL
        struct regulator *vdd;
        struct regulator *vio;
        bool power_enabled;
#endif
#ifdef CONFIG_CALIBRATION
        bool calibration_threshold_readed;
        bool calibration_result;
#endif
};

#if( !defined(CONFIG_STK_PS_ALS_USE_CHANGE_THRESHOLD))
static uint32_t lux_threshold_table[] =
{
        3,
        10,
        40,
        65,
        145,
        300,
        550,
        930,
        1250,
        1700,
};

#define LUX_THD_TABLE_SIZE (sizeof(lux_threshold_table)/sizeof(uint32_t)+1)
static uint16_t code_threshold_table[LUX_THD_TABLE_SIZE+1];
#endif 

static struct i2c_client *stk3x1x_i2c_client = NULL;

static int32_t stk3x1x_enable_ps(struct stk3x1x_data *ps_data, uint8_t enable, uint8_t validate_reg);
static int32_t stk3x1x_enable_als(struct stk3x1x_data *ps_data, uint8_t enable);
static int32_t stk3x1x_set_ps_thd_l(struct stk3x1x_data *ps_data, uint16_t thd_l);
static int32_t stk3x1x_set_ps_thd_h(struct stk3x1x_data *ps_data, uint16_t thd_h);
static int32_t stk3x1x_set_als_thd_l(struct stk3x1x_data *ps_data, uint16_t thd_l);
static int32_t stk3x1x_set_als_thd_h(struct stk3x1x_data *ps_data, uint16_t thd_h);
static int32_t stk3x1x_get_ir_reading(struct stk3x1x_data *ps_data);
#ifdef STK_TUNE0
static int stk_ps_tune_zero_func_fae(struct stk3x1x_data *ps_data);
#endif
#ifdef STK_CHK_REG
static int stk3x1x_validate_n_handle(struct i2c_client *client);
#endif
static int stk_ps_val(struct stk3x1x_data *ps_data);
#ifdef STK_QUALCOMM_POWER_CTRL
static int stk3x1x_device_ctl(struct stk3x1x_data *ps_data, bool enable);
#endif

#ifdef CONFIG_CALIBRATION
static int calibration_threshold_read(struct stk3x1x_data *ps_data);
#endif

static int stk3x1x_i2c_read_data(struct i2c_client *client, unsigned char command, int length, unsigned char *values)
{
        uint8_t retry;
        int err;
        struct i2c_msg msgs[] = 
        {
                {
                        .addr = client->addr,
                        .flags = 0,
                        .len = 1,
                        .buf = &command,
                },
                {
                        .addr = client->addr,
                        .flags = I2C_M_RD,
                        .len = length,
                        .buf = values,
                },
        };

        for (retry = 0; retry < 5; retry++) 
        {
                err = i2c_transfer(client->adapter, msgs, 2);
                if (err == 2)
                        break;
                else
                        mdelay(5);
        }
        //printk("stk3x1x_i2c_read_data");

        if (retry >= 5) 
        {
                printk(KERN_ERR "%s: i2c read fail, err=%d\n", __func__, err);
                return -EIO;
        } 
        return 0;
}

static int stk3x1x_i2c_write_data(struct i2c_client *client, unsigned char command, int length, unsigned char *values)
{
        int retry;
        int err;
        unsigned char data[11];
        struct i2c_msg msg;
        int index;
        //printk("stk3x1x_i2c_write_data\n");

    if (!client)
                return -EINVAL;
    else if (length >= 10) 
        {        
        printk(KERN_ERR "%s:length %d exceeds 10\n", __func__, length);
        return -EINVAL;
    }   

        data[0] = command;
        for (index=1;index<=length;index++)
                data[index] = values[index-1];

        msg.addr = client->addr;
        msg.flags = 0;
        msg.len = length+1;
        msg.buf = data;

        for (retry = 0; retry < 5; retry++) 
        {
                err = i2c_transfer(client->adapter, &msg, 1);
                if (err == 1)
                        break;
                else
                        mdelay(5);
        }

        if (retry >= 5) 
        {
                printk(KERN_ERR "%s: i2c write fail, err=%d\n", __func__, err);
                return -EIO;
        }
        return 0;
}

static int stk3x1x_i2c_smbus_read_byte_data(struct i2c_client *client, unsigned char command)
{
        unsigned char value;
        int err;
        err = stk3x1x_i2c_read_data(client, command, 1, &value);
        if(err < 0)
                return err;
        return value;
}

static int stk3x1x_i2c_smbus_write_byte_data(struct i2c_client *client, unsigned char command, unsigned char value)
{
        int err;
        err = stk3x1x_i2c_write_data(client, command, 1, &value);
        return err;
}



static int stk3x1x_open(struct inode *inode, struct file *file)
{
	file->private_data = stk3x1x_i2c_client;

	if (!file->private_data)
	{
		//APS_ERR("null pointer!!\n");
		return -EINVAL;
	}
	
	return nonseekable_open(inode, file);
}

static int stk3x1x_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static long stk3x1x_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
//		int value;
		int flag;
//		unsigned char *buf;
		void __user *argp = (void __user *)arg;
		struct i2c_client *client = (struct i2c_client*)file->private_data;
        struct stk3x1x_data *ps_data = i2c_get_clientdata(client);		
	
		printk(KERN_ERR "%s: ps io ctrl cmd %d", __func__, _IOC_NR(cmd));
		//ioctl message handle must define by android sensor library (case by case)
		switch(cmd)
		{
			case LTR_IOCTL_GET_PFLAG:
	
				printk(KERN_ERR "%s: LTR_IOCTL_GET_PFLAG\n", __func__);
				flag = (ps_data->ps_enabled) ? (1) : (0);
				if (copy_to_user(argp, &flag, sizeof(flag)))
					return -EFAULT;
	
				printk(KERN_ERR "%s: LTR_IOCTL_GET_PFLAG pflag = %d\n", __func__, flag);
				break;
	
			case LTR_IOCTL_SET_PFLAG:
				printk(KERN_ERR "%s: LTR_IOCTL_SET_PFLAG\n", __func__);

				if (copy_from_user(&flag, argp, sizeof(flag)))
					return -EFAULT;
				if (flag < 0 || flag > 1) {
					return -EINVAL;
				}
	
				//em_sensor_restart_work();
	
				if(flag==1){
					stk3x1x_enable_ps(ps_data, 1, 0);	
				}
				else if(flag==0) {
					stk3x1x_enable_ps(ps_data, 0, 0);	
				}
	
				//elan_sensor_restart_work();
	

				printk(KERN_ERR "%s: LTR_IOCTL_SET_PFLAG pflag = %d\n", __func__, flag);				
				break;
	
			case LTR_IOCTL_GET_LFLAG:
	
				printk(KERN_ERR "%s: LTR_IOCTL_GET_LFLAG\n", __func__);

				flag = (ps_data->als_enabled) ? (1) : (0);
				if (copy_to_user(argp, &flag, sizeof(flag)))
					return -EFAULT;
	
				printk(KERN_ERR "%s: LTR_IOCTL_GET_LFLAG lflag = %d\n", __func__, flag);				

				break;
	
			case LTR_IOCTL_SET_LFLAG:
	
				printk(KERN_ERR "%s: LTR_IOCTL_SET_LFLAG\n", __func__);
				if (copy_from_user(&flag, argp, sizeof(flag))) {
					return -EFAULT;
				}
				if (flag < 0 || flag > 1) {
					return -EINVAL;
				}
	
				if(flag==1){
					stk3x1x_enable_als(ps_data, 1);
				}
				else if(flag==0) {
					stk3x1x_enable_als(ps_data, 0);
				}
	
				printk(KERN_ERR "%s: LTR_IOCTL_SET_LFLAG lflag = %d\n", __func__, flag);				
	
				break;
	
			case LTR_IOCTL_GET_DATA:
				printk(KERN_ERR "%s: LTR_IOCTL_GET_DATA\n", __func__);
				
				break;

			case LTR_IOCTL_GET_CHIPINFO:
				if (copy_to_user(argp, "STK3X1X", sizeof("STK3X1X")))
				{
					printk(KERN_ERR "%s: get delay copy error!\n", __func__);
					return -EFAULT;
				}
				break;
	
			default:
				printk(KERN_ERR "%s: invalid cmd\n", __func__);				
				return -EINVAL;
		}
	
	
	
	
		return 0;
	
	}


static struct file_operations stk3x1x_pls_fops = {
	.owner				= THIS_MODULE,
	.open				= stk3x1x_open,
	.release			= stk3x1x_release,
	.unlocked_ioctl				= stk3x1x_unlocked_ioctl,
};
static struct miscdevice stk3x1x_pls_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "als_ps",
	.fops = &stk3x1x_pls_fops,
};

uint32_t stk_alscode2lux(struct stk3x1x_data *ps_data, uint32_t alscode)
{
        alscode += ((alscode<<7)+(alscode<<3)+(alscode>>1));   
    alscode<<=3; 
    alscode/=ps_data->als_transmittance;
        return alscode;
}

uint32_t stk_lux2alscode(struct stk3x1x_data *ps_data, uint32_t lux)
{
    lux*=ps_data->als_transmittance;
    lux/=1100;
    if (unlikely(lux>=(1<<16)))
        lux = (1<<16) -1;
    return lux;
}

#ifndef CONFIG_STK_PS_ALS_USE_CHANGE_THRESHOLD
static void stk_init_code_threshold_table(struct stk3x1x_data *ps_data)
{
    uint32_t i,j;
    uint32_t alscode;

    code_threshold_table[0] = 0;
#ifdef STK_DEBUG_PRINTF
    printk(KERN_INFO "alscode[0]=%d\n",0);
#endif
    for (i=1,j=0;i<LUX_THD_TABLE_SIZE;i++,j++)
    {
        alscode = stk_lux2alscode(ps_data, lux_threshold_table[j]);
        printk(KERN_INFO "alscode[%d]=%d\n",i,alscode);
        code_threshold_table[i] = (uint16_t)(alscode);
    }
    code_threshold_table[i] = 0xffff;
    printk(KERN_INFO "alscode[%d]=%d\n",i,alscode);
}

static uint32_t stk_get_lux_interval_index(uint16_t alscode)
{
    uint32_t i;
    for (i=1;i<=LUX_THD_TABLE_SIZE;i++)
    {
        if ((alscode>=code_threshold_table[i-1])&&(alscode<code_threshold_table[i]))
        {
            return i;
        }
    }
    return LUX_THD_TABLE_SIZE;
}
#else
void stk_als_set_new_thd(struct stk3x1x_data *ps_data, uint16_t alscode)
{
    int32_t high_thd,low_thd;
    high_thd = alscode + stk_lux2alscode(ps_data, STK_ALS_CHANGE_THD);
    low_thd = alscode - stk_lux2alscode(ps_data, STK_ALS_CHANGE_THD);
    if (high_thd >= (1<<16))
        high_thd = (1<<16) -1;
    if (low_thd <0)
        low_thd = 0;
    stk3x1x_set_als_thd_h(ps_data, (uint16_t)high_thd);
    stk3x1x_set_als_thd_l(ps_data, (uint16_t)low_thd);
}
#endif // CONFIG_STK_PS_ALS_USE_CHANGE_THRESHOLD


static void stk3x1x_proc_plat_data(struct stk3x1x_data *ps_data, struct stk3x1x_platform_data *plat_data)
{
        uint8_t w_reg;

        ps_data->state_reg = plat_data->state_reg;
        ps_data->psctrl_reg = plat_data->psctrl_reg;
#ifdef STK_POLL_PS
        ps_data->psctrl_reg &= 0x3F;
#endif
        ps_data->alsctrl_reg = plat_data->alsctrl_reg;
        ps_data->ledctrl_reg = plat_data->ledctrl_reg;

        ps_data->wait_reg = plat_data->wait_reg;
        if(ps_data->wait_reg < 2)
        {
                printk(KERN_WARNING "%s: wait_reg should be larger than 2, force to write 2\n", __func__);
                ps_data->wait_reg = 2;
        }
        else if (ps_data->wait_reg > 0xFF)
        {
                printk(KERN_WARNING "%s: wait_reg should be less than 0xFF, force to write 0xFF\n", __func__);
                ps_data->wait_reg = 0xFF;
        }
//#ifndef STK_TUNE0
        if(ps_data->ps_thd_h == 0 && ps_data->ps_thd_l == 0)
        {
                ps_data->ps_thd_h = plat_data->ps_thd_h;
                ps_data->ps_thd_l = plat_data->ps_thd_l;
        }
//#endif
#ifdef CALI_PS_EVERY_TIME
        ps_data->ps_high_thd_boot = plat_data->ps_thd_h;
        ps_data->ps_low_thd_boot = plat_data->ps_thd_l;
#endif
        w_reg = 0;
#ifndef STK_POLL_PS
        w_reg |= STK_INT_PS_MODE;
#else
        w_reg |= 0x01;
#endif

#if (!defined(STK_POLL_ALS) && (STK_INT_PS_MODE != 0x02) && (STK_INT_PS_MODE != 0x03))
        w_reg |= STK_INT_ALS;
#endif
        ps_data->int_reg = w_reg;
        return;
}

static int32_t stk3x1x_init_all_reg(struct stk3x1x_data *ps_data)
{
        int32_t ret;

    ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_STATE_REG, ps_data->state_reg);
    if (ret < 0)
    {
        printk(KERN_ERR "%s: write i2c error\n", __func__);
        return ret;
    }
    ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_PSCTRL_REG, ps_data->psctrl_reg);
    if (ret < 0)
    {
        printk(KERN_ERR "%s: write i2c error\n", __func__);
        return ret;
    }
    ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_ALSCTRL_REG, ps_data->alsctrl_reg);
    if (ret < 0)
    {
        printk(KERN_ERR "%s: write i2c error\n", __func__);
        return ret;
    }
    ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_LEDCTRL_REG, ps_data->ledctrl_reg);
    if (ret < 0)
    {
        printk(KERN_ERR "%s: write i2c error\n", __func__);
        return ret;
    }
    ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_WAIT_REG, ps_data->wait_reg);
    if (ret < 0)
    {
        printk(KERN_ERR "%s: write i2c error\n", __func__);
        return ret;
    }
#ifdef STK_TUNE0
        ps_data->psa = 0x0;
        ps_data->psi = 0xFFFF;
#endif
//#else
        stk3x1x_set_ps_thd_h(ps_data, ps_data->ps_thd_h);
        stk3x1x_set_ps_thd_l(ps_data, ps_data->ps_thd_l);
//#endif

    ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_INT_REG, ps_data->int_reg);
    if (ret < 0)
        {
                printk(KERN_ERR "%s: write i2c error\n", __func__);
                return ret;
        }

        /*
    ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, 0x87, 0x60);
    if (ret < 0)
        {
                printk(KERN_ERR "%s: write i2c error\n", __func__);
                return ret;
        }
        */

        return 0;
}


static int32_t stk3x1x_check_pid(struct stk3x1x_data *ps_data)
{
        unsigned char value[2], pid_msb;
        int err;

        err = stk3x1x_i2c_read_data(ps_data->client, STK_PDT_ID_REG, 2, &value[0]);
        if(err < 0)
        {
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, err);
                return err;
        }

        printk(KERN_INFO "%s: PID=0x%x, RID=0x%x\n", __func__, value[0], value[1]);

        if(value[1] == 0xC0)
                printk(KERN_INFO "%s: RID=0xC0!!!!!!!!!!!!!\n", __func__);

        if(value[0] == 0)
        {
                printk(KERN_ERR "PID=0x0, please make sure the chip is stk3x1x!\n");
                return -2;
        }

        pid_msb = value[0] & 0xF0;
        switch(pid_msb)
        {
        case 0x10:
        case 0x20:
        case 0x30:
                return 0;
        default:
                printk(KERN_ERR "%s: invalid PID(%#x)\n", __func__, value[0]);
                return -1;
        }
        return 0;
}


static int32_t stk3x1x_software_reset(struct stk3x1x_data *ps_data)
{
    int32_t r;
    uint8_t w_reg;

    w_reg = 0x7F;
    r = stk3x1x_i2c_smbus_write_byte_data(ps_data->client,STK_WAIT_REG,w_reg);
    if (r<0)
    {
        printk(KERN_ERR "%s: software reset: write i2c error, ret=%d\n", __func__, r);
        return r;
    }
    r = stk3x1x_i2c_smbus_read_byte_data(ps_data->client,STK_WAIT_REG);
    if (w_reg != r)
    {
        printk(KERN_ERR "%s: software reset: read-back value is not the same\n", __func__);
        return -1;
    }

    r = stk3x1x_i2c_smbus_write_byte_data(ps_data->client,STK_SW_RESET_REG,0);
    if (r<0)
    {
        printk(KERN_ERR "%s: software reset: read error after reset\n", __func__);
        return r;
    }
        usleep_range(1000, 5000);
    return 0;
}


static int32_t stk3x1x_set_als_thd_l(struct stk3x1x_data *ps_data, uint16_t thd_l)
{
        unsigned char val[2];
        int ret;
        val[0] = (thd_l & 0xFF00) >> 8;
        val[1] = thd_l & 0x00FF;
        ret = stk3x1x_i2c_write_data(ps_data->client, STK_THDL1_ALS_REG, 2, val);
        if(ret < 0)
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, ret);
        return ret;
}
static int32_t stk3x1x_set_als_thd_h(struct stk3x1x_data *ps_data, uint16_t thd_h)
{
        unsigned char val[2];
        int ret;
        val[0] = (thd_h & 0xFF00) >> 8;
        val[1] = thd_h & 0x00FF;
        ret = stk3x1x_i2c_write_data(ps_data->client, STK_THDH1_ALS_REG, 2, val);
        if(ret < 0)
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, ret);
        return ret;
}

static int32_t stk3x1x_set_ps_thd_l(struct stk3x1x_data *ps_data, uint16_t thd_l)
{
        unsigned char val[2];
        int ret;
        val[0] = (thd_l & 0xFF00) >> 8;
        val[1] = thd_l & 0x00FF;
        ret = stk3x1x_i2c_write_data(ps_data->client, STK_THDL1_PS_REG, 2, val);
        if(ret < 0)
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, ret);
        return ret;
}
static int32_t stk3x1x_set_ps_thd_h(struct stk3x1x_data *ps_data, uint16_t thd_h)
{
        unsigned char val[2];
        int ret;
        val[0] = (thd_h & 0xFF00) >> 8;
        val[1] = thd_h & 0x00FF;
        ret = stk3x1x_i2c_write_data(ps_data->client, STK_THDH1_PS_REG, 2, val);
        if(ret < 0)
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, ret);
        return ret;
}

static uint32_t stk3x1x_get_ps_reading(struct stk3x1x_data *ps_data)
{
        unsigned char value[2];
        int err;
        err = stk3x1x_i2c_read_data(ps_data->client, STK_DATA1_PS_REG, 2, &value[0]);
        if(err < 0)
        {
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, err);
                return err;
        }
        return ((value[0]<<8) | value[1]);
}


static int32_t stk3x1x_set_flag(struct stk3x1x_data *ps_data, uint8_t org_flag_reg, uint8_t clr)
{
        uint8_t w_flag;
        int ret;

        w_flag = org_flag_reg | (STK_FLG_ALSINT_MASK | STK_FLG_PSINT_MASK | STK_FLG_OUI_MASK | STK_FLG_IR_RDY_MASK);
        w_flag &= (~clr);
        //printk(KERN_INFO "%s: org_flag_reg=0x%x, w_flag = 0x%x\n", __func__, org_flag_reg, w_flag);
    ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client,STK_FLAG_REG, w_flag);
        if(ret < 0)
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, ret);
        return ret;
}

static int32_t stk3x1x_get_flag(struct stk3x1x_data *ps_data)
{
        int ret;
    ret = stk3x1x_i2c_smbus_read_byte_data(ps_data->client,STK_FLAG_REG);
        if(ret < 0)
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, ret);
        return ret;
}

#ifdef STK_GES
static int32_t stk3x1x_set_flag2(struct stk3x1x_data *ps_data, uint8_t org_flag2_reg, uint8_t clr)
{
        uint8_t w_flag2;
        int ret;

        w_flag2 = org_flag2_reg | 0x72;
        w_flag2 &= (~clr);
        //printk(KERN_INFO "%s: org_flag2_reg=0x%x, w_flag2 = 0x%x\n", __func__, org_flag2_reg, w_flag2);
    ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client,STK_FLAG2_REG, w_flag2);
        if(ret < 0)
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, ret);
        return ret;
}

static int32_t stk3x1x_get_flag2(struct stk3x1x_data *ps_data)
{
        int ret;
    ret = stk3x1x_i2c_smbus_read_byte_data(ps_data->client,STK_FLAG2_REG);
        if(ret < 0)
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, ret);
        return ret;
}
#endif

static int32_t stk3x1x_set_state(struct stk3x1x_data *ps_data, uint8_t state)
{
        int ret;
    ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client,STK_STATE_REG, state);
        if(ret < 0)
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, ret);
        return ret;
}

static int32_t stk3x1x_get_state(struct stk3x1x_data *ps_data)
{
        int ret;
    ret = stk3x1x_i2c_smbus_read_byte_data(ps_data->client,STK_STATE_REG);
        if(ret < 0)
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, ret);
        return ret;
}

#ifdef STK_GES
static int32_t stk3x1x_set_gsctrl(struct stk3x1x_data *ps_data, uint8_t state)
{
        int ret;
    ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client,STK_GSCTRL_REG, state);
        if(ret < 0)
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, ret);
        return ret;
}

static int32_t stk3x1x_get_gsctrl(struct stk3x1x_data *ps_data)
{
        int ret;
    ret = stk3x1x_i2c_smbus_read_byte_data(ps_data->client,STK_GSCTRL_REG);
        if(ret < 0)
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, ret);
        return ret;
}

#if 0
static uint32_t stk3x1x_get_ges_reading(struct stk3x1x_data *ps_data, unsigned int *ges0,
                                unsigned int *ges1, unsigned int *ges2)
{
        int retry, len, no = 0;
        uint8_t reg_val_crit;
        int err;
        unsigned char value[10];

        while(stk_ges_op[no].ops[1] != 0)
        {
                retry = stk_ges_op[no].action.rw_len_retry & 0x0F;
                len = (stk_ges_op[no].action.rw_len_retry & 0x70) >> 4;
                reg_val_crit = stk_ges_op[no].action.reg_value_retry_crit;
                if(stk_ges_op[no].action.rw_len_retry & 0x80)
                {
                        while(retry != 0)
                        {
                                err = stk3x1x_i2c_read_data(ps_data->client, 
                                                                stk_ges_op[no].action.reg, len, value);
                                if(err < 0)
                                {
                                        printk(KERN_ERR "%s: fail, ret=%d\n", __func__, err);
                                        return err;
                                }
                                if(reg_val_crit)
                                {
                                        if(value[0] & reg_val_crit)
                                                break;
                                }
                                if(stk_ges_op[no].action.sleep_10ns != 0)
                                        usleep_range(stk_ges_op[no].action.sleep_10ns*10, 
                                                                        stk_ges_op[no].action.sleep_10ns*10+300);
                                retry--;
                        }
                }
                else
                {
                        while(retry != 0)
                        {
                                err = stk3x1x_i2c_write_data(ps_data->client, stk_ges_op[no].action.reg, 
                                                                len, &reg_val_crit);
                                if(err < 0)
                                {
                                        printk(KERN_ERR "%s: fail, err=%d\n", __func__, err);
                                        return err;
                                }

                                if(stk_ges_op[no].action.sleep_10ns != 0)
                                        usleep_range(stk_ges_op[no].action.sleep_10ns*10, 
                                                                        stk_ges_op[no].action.sleep_10ns*10+300);
                                retry--;
                        }
                }

                if(stk_ges_op[no].action.reg == 0x24)
                {
                        *ges0 = (value[0]<<8) | value[1];
                        *ges1 = (value[2]<<8) | value[3];
                }
                else if(stk_ges_op[no].action.reg == stk_ges_op[9].ops[0])
                {
                        *ges2 = (value[0]<<8) | value[1];
                }

                no++;
        }
        return 0;
}

#else
static uint32_t stk3x1x_get_ges_reading(struct stk3x1x_data *ps_data, unsigned int *ges0,unsigned int *ges1,unsigned int *ges2)
{
        unsigned char value[4];
        int err, retry = 10;

        do {
                err = stk3x1x_get_flag(ps_data);
                if(err < 0)
                        return err;
                if(err & STK_FLG_PSDR_MASK)
                        break;
                //printk(KERN_INFO "ges: not ready, %d\n", retry);
                retry--;
                usleep_range(350, 1000);
        } while(retry > 0);
        err = stk3x1x_i2c_read_data(ps_data->client, STK_DATA1_PS_REG, 2, &value[0]);

        err = stk3x1x_i2c_read_data(ps_data->client, 0x24, 4, &value[0]);
        if(err < 0)
        {
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, err);
                return err;
        }
        *ges0 = (value[0]<<8) | value[1];
        *ges1 = (value[2]<<8) | value[3];
        //printk(KERN_INFO "%s: ges=%d,%d\n",__func__, *ges0, *ges1);
        return 0;
}
#endif

static int32_t stk3x1x_enable_ges(struct stk3x1x_data *ps_data, uint8_t enable, uint8_t mode)
{
    int32_t ret;
        uint8_t w_state_reg, gsctrl_reg;
        uint8_t org_mode = 0;

        if(ps_data->ps_enabled)
        {
                printk(KERN_INFO "%s: since PS is enabled, ges is disabled\n", __func__);
                ps_data->re_enable_ges = enable;
                return 0;
        }

        if(enable == ps_data->ges_enabled)
                return 0;

        if(enable)
        {
#ifdef STK_QUALCOMM_POWER_CTRL
                ret = stk3x1x_device_ctl(ps_data, enable);
                if (ret)
                        return ret;
#endif
                if(ps_data->als_enabled) 
                {
                        printk(KERN_INFO "%s: force disable ALS\n", __func__);
                        stk3x1x_enable_als(ps_data, 0);
                        ps_data->re_enable_als = true;
                }

                ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_WAIT_REG, 0);
                if (ret < 0)
                {
                        printk(KERN_ERR "%s: write i2c error\n", __func__);
                        return ret;
                }

                ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_INT_REG, 0);
                if (ret < 0)
                {
                        printk(KERN_ERR "%s: write i2c error\n", __func__);
                        return ret;
                }

                w_state_reg = STK_STATE_EN_WAIT_MASK | STK_STATE_EN_PS_MASK; 
                ret = stk3x1x_set_state(ps_data, w_state_reg);
                if(ret < 0)
                        return ret;

                if(mode == 2)
                {
                        ret = stk3x1x_get_gsctrl(ps_data);
                        if(ret < 0)
                                return ret;
                        gsctrl_reg = ret & 0xF3;
#ifdef STK_POLL_PS
                        gsctrl_reg |= 0x04;
#else
                        gsctrl_reg |= 0x0C;
#endif
                        ret = stk3x1x_set_gsctrl(ps_data, gsctrl_reg);
                        if(ret < 0)
                                return ret;
#ifdef STK_POLL_PS
                        hrtimer_start(&ps_data->ps_timer, ps_data->ps_poll_delay, HRTIMER_MODE_REL);
#else
                        enable_irq(ps_data->irq);
#endif
                }
                ps_data->ges_enabled = mode;
        }
        else
        {
                org_mode = ps_data->ges_enabled;
                if(org_mode == 2)
                {
#ifdef STK_POLL_PS
                        hrtimer_cancel(&ps_data->ps_timer);
                        cancel_work_sync(&ps_data->stk_ps_work);
#else
                        disable_irq(ps_data->irq);
#endif
                }

                ret = stk3x1x_set_state(ps_data, 0);
                if(ret < 0)
                        return ret;

                ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_WAIT_REG, ps_data->wait_reg);
                if (ret < 0)
                {
                        printk(KERN_ERR "%s: write i2c error\n", __func__);
                        return ret;
                }

                ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_INT_REG, ps_data->int_reg);
                if (ret < 0)
                {
                        printk(KERN_ERR "%s: write i2c error\n", __func__);
                        return ret;
                }
                if(org_mode == 2)
                {
                        ret = stk3x1x_get_gsctrl(ps_data);
                        if(ret < 0)
                                return ret;
                        gsctrl_reg = ret & (~0x0C);
                        ret = stk3x1x_set_gsctrl(ps_data, gsctrl_reg);
                        if(ret < 0)
                                return ret;
                }
                ps_data->ges_enabled = 0;
                if(ps_data->re_enable_als) 
                {
                        printk(KERN_INFO "%s: re-enable ALS\n", __func__);
                        stk3x1x_enable_als(ps_data, 1);
                        ps_data->re_enable_als = false;
                }
#ifdef STK_QUALCOMM_POWER_CTRL
                ret = stk3x1x_device_ctl(ps_data, enable);
                if (ret)
                        return ret;
#endif
        }

        return 0;
}
#endif /* #ifdef STK_GES */

static int32_t stk3x1x_enable_ps(struct stk3x1x_data *ps_data, uint8_t enable, uint8_t validate_reg)
{
    int32_t ret;
        uint8_t w_state_reg;
        uint8_t curr_ps_enable;
        uint32_t reading;
        int32_t near_far_state;

#ifdef STK_QUALCOMM_POWER_CTRL
        if (enable) {
                ret = stk3x1x_device_ctl(ps_data, enable);
                if (ret)
                        return ret;
        }
#endif

#ifdef STK_CHK_REG
        if(validate_reg)
        {
                ret = stk3x1x_validate_n_handle(ps_data->client);
                if(ret < 0)
                        printk(KERN_ERR "stk3x1x_validate_n_handle fail: %d\n", ret); 
        }
#endif /* #ifdef STK_CHK_REG */

#ifdef STK_GES
        if(ps_data->ges_enabled && enable)
        {
                printk(KERN_INFO "%s: force disable ges\n", __func__);
                stk3x1x_enable_ges(ps_data, 0, 1);
                ps_data->re_enable_ges = 1;
        }
#endif
        curr_ps_enable = ps_data->ps_enabled?1:0;
        if(curr_ps_enable == enable)
                return 0;

#ifdef STK_TUNE0
        if (!(ps_data->psi_set) && !enable)
        {
                hrtimer_cancel(&ps_data->ps_tune0_timer);
                cancel_work_sync(&ps_data->stk_ps_tune0_work);
        }
#endif
        if(ps_data->first_boot == true)
        {
                ps_data->first_boot = false;
        }

        ret = stk3x1x_get_state(ps_data);
        if(ret < 0)
                return ret;
        w_state_reg = ret;


        w_state_reg &= ~(STK_STATE_EN_PS_MASK | STK_STATE_EN_WAIT_MASK | STK_STATE_EN_AK_MASK); 
        if(enable)
        {
                w_state_reg |= STK_STATE_EN_PS_MASK;
                if(!(ps_data->als_enabled))
                        w_state_reg |= STK_STATE_EN_WAIT_MASK;
        }
        ret = stk3x1x_set_state(ps_data, w_state_reg);
        if(ret < 0)
                return ret;

    if(enable)
        {
#ifdef CONFIG_CALIBRATION
	#ifndef CALI_PS_EVERY_TIME
			if(!ps_data->calibration_threshold_readed)
	#endif
				if(calibration_threshold_read(ps_data) < 0)
					pr_err("Error:(%s) calibration_threshold_read failed!\n", __func__);
#endif
#ifdef STK_TUNE0
        #ifdef CALI_PS_EVERY_TIME
                ps_data->psi_set = 0;
                ps_data->psa = 0;
                ps_data->psi = 0xFFFF;
                #ifndef QUALCOMM_PLATFORM
                ps_data->ps_thd_h = ps_data->ps_high_thd_boot;
                ps_data->ps_thd_l = ps_data->ps_low_thd_boot;
                #endif
                hrtimer_start(&ps_data->ps_tune0_timer, ps_data->ps_tune0_delay, HRTIMER_MODE_REL);
        #else

                if (!(ps_data->psi_set))
                        hrtimer_start(&ps_data->ps_tune0_timer, ps_data->ps_tune0_delay, HRTIMER_MODE_REL);
        #endif  /* #ifdef CALI_PS_EVERY_TIME */
                stk3x1x_set_ps_thd_h(ps_data, ps_data->ps_thd_h);
                stk3x1x_set_ps_thd_l(ps_data, ps_data->ps_thd_l);
#endif
                printk(KERN_INFO "%s: HT=%d,LT=%d\n", __func__, ps_data->ps_thd_h,  ps_data->ps_thd_l);
#ifdef STK_POLL_PS
                hrtimer_start(&ps_data->ps_timer, ps_data->ps_poll_delay, HRTIMER_MODE_REL);
                ps_data->ps_distance_last = -1;
#endif
#ifdef PS_VALUE_LOG
                hrtimer_start(&ps_data->ps_log_timer, ps_data->ps_poll_delay, HRTIMER_MODE_REL);
#endif
#ifndef STK_POLL_PS
        #ifndef STK_POLL_ALS
                if(!(ps_data->als_enabled))
        #endif  /* #ifndef STK_POLL_ALS */
                        enable_irq(ps_data->irq);
#endif  /* #ifndef STK_POLL_PS */
                ps_data->ps_enabled = true;
#ifdef STK_CHK_REG
                if(!validate_reg)
                {
                        ps_data->ps_distance_last = 1;
                        input_report_abs(ps_data->ps_input_dev, ABS_DISTANCE, 1);
                        input_sync(ps_data->ps_input_dev);
                       // wake_lock_timeout(&ps_data->ps_wakelock, 3*HZ);
                        reading = stk3x1x_get_ps_reading(ps_data);
                        printk(KERN_INFO "%s: force report ps input event=1, ps code = %d\n",__func__, reading);
                }
                else
#endif /* #ifdef STK_CHK_REG */
                {
                        usleep_range(4000, 5000);
                        ret = stk3x1x_get_flag(ps_data);
                        if (ret < 0)
                                return ret;
                        near_far_state = ret & STK_FLG_NF_MASK;
                        ps_data->ps_distance_last = near_far_state;
                        input_report_abs(ps_data->ps_input_dev, ABS_DISTANCE, near_far_state);
                        input_sync(ps_data->ps_input_dev);
                     //   wake_lock_timeout(&ps_data->ps_wakelock, 3*HZ);
                        reading = stk3x1x_get_ps_reading(ps_data);
                        printk(KERN_INFO "%s: ps input event=%d, ps code = %d\n",__func__, near_far_state, reading);
                }
        }
        else
        {
#ifdef PS_VALUE_LOG
                hrtimer_cancel(&ps_data->ps_log_timer);
#endif
#ifdef STK_POLL_PS
                hrtimer_cancel(&ps_data->ps_timer);
                cancel_work_sync(&ps_data->stk_ps_work);
#else
#ifndef STK_POLL_ALS
                if(!(ps_data->als_enabled))
#endif
                        disable_irq(ps_data->irq);
#endif
                ps_data->ps_enabled = false;
#ifdef STK_GES
                if(ps_data->re_enable_ges)
                {
                        printk(KERN_INFO "%s: re-enable ges\n", __func__);
                        stk3x1x_enable_ges(ps_data, 1, 1);
                        ps_data->re_enable_ges = 0;
                }
#endif
#ifdef STK_QUALCOMM_POWER_CTRL
                ret = stk3x1x_device_ctl(ps_data, enable);
                if (ret)
                        return ret;
#endif
        }
        return ret;
}

static int32_t stk3x1x_enable_als(struct stk3x1x_data *ps_data, uint8_t enable)
{
    int32_t ret;
        uint8_t w_state_reg;
        uint8_t curr_als_enable = (ps_data->als_enabled)?1:0;

#ifdef STK_GES
        if(ps_data->ges_enabled)
        {
                printk(KERN_INFO "%s: since ges is enabled, ALS is disabled\n", __func__);
                ps_data->re_enable_als = enable ? true : false;
                return 0;
        }
#endif  /* #ifdef STK_GES */
        if(curr_als_enable == enable)
                return 0;
#ifdef STK_QUALCOMM_POWER_CTRL
        if (enable) {
                ret = stk3x1x_device_ctl(ps_data, enable);
                if (ret)
                        return ret;
        }
#endif
#ifndef STK_POLL_ALS
        #ifdef STK_IRS
        if(enable && !(ps_data->ps_enabled))
        {
                ret = stk3x1x_get_ir_reading(ps_data);
                if(ret > 0)
                        ps_data->ir_code = ret;
        }
        #endif

    if (enable)
        {
        stk3x1x_set_als_thd_h(ps_data, 0x0000);
        stk3x1x_set_als_thd_l(ps_data, 0xFFFF);
        }
#endif

        ret = stk3x1x_get_state(ps_data);
        if(ret < 0)
                return ret;

        w_state_reg = (uint8_t)(ret & (~(STK_STATE_EN_ALS_MASK | STK_STATE_EN_WAIT_MASK))); 
        if(enable)
                w_state_reg |= STK_STATE_EN_ALS_MASK;
        else if (ps_data->ps_enabled)
                w_state_reg |= STK_STATE_EN_WAIT_MASK;

        ret = stk3x1x_set_state(ps_data, w_state_reg);
        if(ret < 0)
                return ret;

    if (enable)
    {
                ps_data->als_enabled = true;
#ifdef STK_POLL_ALS
                hrtimer_start(&ps_data->als_timer, ps_data->als_poll_delay, HRTIMER_MODE_REL);
#else
#ifndef STK_POLL_PS
                if(!(ps_data->ps_enabled))
#endif
                        enable_irq(ps_data->irq);
#endif
#ifdef STK_IRS
                ps_data->als_data_index = 0;
#endif
    }
        else
        {
                ps_data->als_enabled = false;
#ifdef STK_POLL_ALS
                hrtimer_cancel(&ps_data->als_timer);
                cancel_work_sync(&ps_data->stk_als_work);
#else
#ifndef STK_POLL_PS
                if(!(ps_data->ps_enabled))
#endif
                        disable_irq(ps_data->irq);
#endif
#ifdef STK_QUALCOMM_POWER_CTRL
                ret = stk3x1x_device_ctl(ps_data, enable);
                if (ret)
                        return ret;
#endif
        }
    return ret;
}

static int32_t stk3x1x_get_als_reading(struct stk3x1x_data *ps_data)
{
    int32_t word_data;
#ifdef STK_ALS_FIR
        int index;   
        int firlen = atomic_read(&ps_data->firlength);   
#endif
        unsigned char value[2];
        int ret;

        ret = stk3x1x_i2c_read_data(ps_data->client, STK_DATA1_ALS_REG, 2, &value[0]);
        if(ret < 0)
        {
                printk(KERN_ERR "%s fail, ret=0x%x", __func__, ret);
                return ret;
        }
        word_data = (value[0]<<8) | value[1];

#ifdef STK_ALS_FIR
        if(ps_data->fir.number < firlen)
        {                
                ps_data->fir.raw[ps_data->fir.number] = word_data;
                ps_data->fir.sum += word_data;
                ps_data->fir.number++;
                ps_data->fir.idx++;
        }
        else
        {
                index = ps_data->fir.idx % firlen;
                ps_data->fir.sum -= ps_data->fir.raw[index];
                ps_data->fir.raw[index] = word_data;
                ps_data->fir.sum += word_data;
                ps_data->fir.idx++;
                word_data = ps_data->fir.sum/firlen;
        }
#endif

        return word_data;
}

static int32_t stk3x1x_set_irs_it_slp(struct stk3x1x_data *ps_data, uint16_t *slp_time)
{
        uint8_t irs_alsctrl;
        int32_t ret;

        irs_alsctrl = (ps_data->alsctrl_reg & 0x0F) - 2;
        switch(irs_alsctrl)
        {
                case 6:
                        *slp_time = 12;
                        break;
                case 7:
                        *slp_time = 24;
                        break;
                case 8:
                        *slp_time = 48;
                        break;
                case 9:
                        *slp_time = 96;
                        break;
                default:
                        printk(KERN_ERR "%s: unknown ALS IT=0x%x\n", __func__, irs_alsctrl);
                        ret = -EINVAL;
                        return ret;
        }
        irs_alsctrl |= (ps_data->alsctrl_reg & 0xF0);
        ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_ALSCTRL_REG, irs_alsctrl);
        if (ret < 0)
        {
                printk(KERN_ERR "%s: write i2c error\n", __func__);
                return ret;
        }
        return 0;
}

static int32_t stk3x1x_get_ir_reading(struct stk3x1x_data *ps_data)
{
    int32_t word_data, ret;
        uint8_t w_reg, retry = 0;
        uint16_t irs_slp_time = 100;
        //bool re_enable_ps = false;
        //bool re_enable_tune0 = false;
        unsigned char value[2];

        // if(ps_data->ps_enabled)
        // {
// #ifdef STK_TUNE0
                // if (!(ps_data->psi_set))
                // {
                        // hrtimer_cancel(&ps_data->ps_tune0_timer);
                        // cancel_work_sync(&ps_data->stk_ps_tune0_work);
                        // re_enable_tune0 = true;
                // }
// #endif
                //stk3x1x_enable_ps(ps_data, 0, 1);
                //re_enable_ps = true;
        // }

        ret = stk3x1x_set_irs_it_slp(ps_data, &irs_slp_time);
        if(ret < 0)
                goto irs_err_i2c_rw;

        ret = stk3x1x_get_state(ps_data);
        if(ret < 0)
                goto irs_err_i2c_rw;

        w_reg = ret | STK_STATE_EN_IRS_MASK;
        ret = stk3x1x_set_state(ps_data, w_reg);
        if(ret < 0)
                goto irs_err_i2c_rw;
        msleep(irs_slp_time);

        do
        {
                usleep_range(3000, 4000);
                //msleep(3);
                ret = stk3x1x_get_flag(ps_data);
                if (ret < 0)
                        goto irs_err_i2c_rw;
                retry++;
        }while(retry < 10 && ((ret&STK_FLG_IR_RDY_MASK) == 0));

        if(retry == 10)
        {
                printk(KERN_ERR "%s: ir data is not ready for 300ms\n", __func__);
                ret = -EINVAL;
                goto irs_err_i2c_rw;
        }

        ret = stk3x1x_set_flag(ps_data, ret, STK_FLG_IR_RDY_MASK);
    if (ret < 0)
                goto irs_err_i2c_rw;

        ret = stk3x1x_i2c_read_data(ps_data->client, STK_DATA1_IR_REG, 2, &value[0]);
        if(ret < 0)
        {
                printk(KERN_ERR "%s fail, ret=0x%x", __func__, ret);
                goto irs_err_i2c_rw;
        }
        word_data = ((value[0]<<8) | value[1]);
        //printk(KERN_INFO "%s: ir=%d\n", __func__, word_data);

        ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_ALSCTRL_REG, ps_data->alsctrl_reg );
        if (ret < 0)
        {
                printk(KERN_ERR "%s: write i2c error\n", __func__);
                goto irs_err_i2c_rw;
        }

// #ifdef STK_TUNE0
        // if (re_enable_tune0 == true)
        // {
                // hrtimer_start(&ps_data->ps_tune0_timer, ps_data->ps_tune0_delay, HRTIMER_MODE_REL);
        // }
// #endif

        // if(re_enable_ps)
                // stk3x1x_enable_ps(ps_data, 1, 1);
        return word_data;

irs_err_i2c_rw:
        // if(re_enable_ps)
                // stk3x1x_enable_ps(ps_data, 1, 1);
        return ret;
}

#ifdef STK_CHK_REG
static int stk3x1x_chk_reg_valid(struct stk3x1x_data *ps_data) 
{
        unsigned char value[9];
        int err;
        /*
        uint8_t cnt;

        for(cnt=0;cnt<9;cnt++)
        {
                value[cnt] = stk3x1x_i2c_smbus_read_byte_data(ps_data->client, (cnt+1));
                if(value[cnt] < 0)
                {
                        printk(KERN_ERR "%s fail, ret=%d", __func__, value[cnt]);
                        return value[cnt];
                }
        }
        */
        err = stk3x1x_i2c_read_data(ps_data->client, STK_PSCTRL_REG, 9, &value[0]);
        if(err < 0)
        {
                printk(KERN_ERR "%s: fail, ret=%d\n", __func__, err);
                return err;
        }

        if(value[0] != ps_data->psctrl_reg)
        {
                printk(KERN_ERR "%s: invalid reg 0x01=0x%2x\n", __func__, value[0]);
                return 0xFF;
        }
        if(value[1] != ps_data->alsctrl_reg)
        {
                printk(KERN_ERR "%s: invalid reg 0x02=0x%2x\n", __func__, value[1]);
                return 0xFF;
        }
        if(value[2] != ps_data->ledctrl_reg)
        {
                printk(KERN_ERR "%s: invalid reg 0x03=0x%2x\n", __func__, value[2]);
                return 0xFF;
        }
        if(value[3] != ps_data->int_reg)
        {
                printk(KERN_ERR "%s: invalid reg 0x04=0x%2x\n", __func__, value[3]);
                return 0xFF;
        }
        if(value[4] != ps_data->wait_reg)
        {
                printk(KERN_ERR "%s: invalid reg 0x05=0x%2x\n", __func__, value[4]);
                return 0xFF;
        }
        if(value[5] != ((ps_data->ps_thd_h & 0xFF00) >> 8))
        {
                printk(KERN_ERR "%s: invalid reg 0x06=0x%2x\n", __func__, value[5]);
                return 0xFF;
        }
        if(value[6] != (ps_data->ps_thd_h & 0x00FF))
        {
                printk(KERN_ERR "%s: invalid reg 0x07=0x%2x\n", __func__, value[6]);
                return 0xFF;
        }
        if(value[7] != ((ps_data->ps_thd_l & 0xFF00) >> 8))
        {
                printk(KERN_ERR "%s: invalid reg 0x08=0x%2x\n", __func__, value[7]);
                return 0xFF;
        }
        if(value[8] != (ps_data->ps_thd_l & 0x00FF))
        {
                printk(KERN_ERR "%s: invalid reg 0x09=0x%2x\n", __func__, value[8]);
                return 0xFF;
        }

        return 0;
}

static int stk3x1x_validate_n_handle(struct i2c_client *client) 
{
        struct stk3x1x_data *ps_data = i2c_get_clientdata(client);
        int err;

        err = stk3x1x_chk_reg_valid(ps_data);
        if(err < 0)
        {
                printk(KERN_ERR "stk3x1x_chk_reg_valid fail: %d\n", err);        
                return err;
        }

        if(err == 0xFF)
        {
                printk(KERN_ERR "%s: Re-init chip\n", __func__);
                err = stk3x1x_software_reset(ps_data); 
                if(err < 0)
                        return err;
                err = stk3x1x_init_all_reg(ps_data);
                if(err < 0)
                        return err;

                //ps_data->psa = 0;
                //ps_data->psi = 0xFFFF;
                stk3x1x_set_ps_thd_h(ps_data, ps_data->ps_thd_h);
                stk3x1x_set_ps_thd_l(ps_data, ps_data->ps_thd_l);
#ifdef STK_ALS_FIR
                memset(&ps_data->fir, 0x00, sizeof(ps_data->fir));  
#endif
                return 0xFF;
        }
        return 0;
}
#endif /* #ifdef STK_CHK_REG */

static ssize_t stk_als_code_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
    int32_t reading;

    reading = stk3x1x_get_als_reading(ps_data);
    return scnprintf(buf, PAGE_SIZE, "%d\n", reading);
}


static ssize_t stk_als_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
    int32_t ret;

        ret = stk3x1x_get_state(ps_data);
        if(ret < 0)
                return ret;
    ret = (ret & STK_STATE_EN_ALS_MASK)?1:0;

        return scnprintf(buf, PAGE_SIZE, "%d\n", ret);
}

static ssize_t stk_als_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data = dev_get_drvdata(dev);
        uint8_t en;
        if (sysfs_streq(buf, "1"))
                en = 1;
        else if (sysfs_streq(buf, "0"))
                en = 0;
        else 
        {
                printk(KERN_ERR "%s, invalid value %d\n", __func__, *buf);
                return -EINVAL;
        }
    printk(KERN_INFO "%s: Enable ALS : %d\n", __func__, en);
    mutex_lock(&ps_data->io_lock);
    stk3x1x_enable_als(ps_data, en);
    mutex_unlock(&ps_data->io_lock);
    return size;
}

static ssize_t stk_als_lux_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data = dev_get_drvdata(dev);
    int32_t als_reading;
        uint32_t als_lux;
    als_reading = stk3x1x_get_als_reading(ps_data);    
        als_lux = stk_alscode2lux(ps_data, als_reading);
    return scnprintf(buf, PAGE_SIZE, "%d lux\n", als_lux);
}

static ssize_t stk_als_lux_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        unsigned long value = 0;
        int ret;
        ret = kstrtoul(buf, 16, &value);
        if(ret < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=0x%x\n", __func__, ret);
                return ret;
        }
    ps_data->als_lux_last = value;
        input_report_abs(ps_data->als_input_dev, ABS_MISC, value);
        input_sync(ps_data->als_input_dev);
        printk(KERN_INFO "%s: als input event %ld lux\n",__func__, value);

    return size;
}


static ssize_t stk_als_transmittance_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
    int32_t transmittance;
    transmittance = ps_data->als_transmittance;
    return scnprintf(buf, PAGE_SIZE, "%d\n", transmittance);
}


static ssize_t stk_als_transmittance_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        unsigned long value = 0;
        int ret;
        ret = kstrtoul(buf, 10, &value);
        if(ret < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=0x%x\n", __func__, ret);
                return ret;         
        }
    ps_data->als_transmittance = value;
    return size;
}

static ssize_t stk_als_delay_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        int64_t delay;
        mutex_lock(&ps_data->io_lock);
        delay = ktime_to_ns(ps_data->als_poll_delay);
        mutex_unlock(&ps_data->io_lock);
        return scnprintf(buf, PAGE_SIZE, "%lld\n", delay);
}


static ssize_t stk_als_delay_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    uint64_t value = 0;
        int ret;
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        ret = kstrtoull(buf, 10, &value);
        if(ret < 0)
        {
                printk(KERN_ERR "%s:kstrtoull failed, ret=0x%x\n", __func__, ret);
                return ret;
        }
#ifdef STK_DEBUG_PRINTF
        printk(KERN_INFO "%s: set als poll delay=%lld\n", __func__, value);
#endif
        if(value < MIN_ALS_POLL_DELAY_NS)
        {
                printk(KERN_ERR "%s: delay is too small\n", __func__);
                value = MIN_ALS_POLL_DELAY_NS;
        }
        mutex_lock(&ps_data->io_lock);
        if(value != ktime_to_ns(ps_data->als_poll_delay))
                ps_data->als_poll_delay = ns_to_ktime(value);
#ifdef STK_ALS_FIR
        ps_data->fir.number = 0;
        ps_data->fir.idx = 0;
        ps_data->fir.sum = 0;
#endif
        mutex_unlock(&ps_data->io_lock);
        return size;
}

static ssize_t stk_als_ir_code_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
    int32_t reading;
    reading = stk3x1x_get_ir_reading(ps_data);
    return scnprintf(buf, PAGE_SIZE, "%d\n", reading);
}

#ifdef STK_ALS_FIR
static ssize_t stk_als_firlen_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        int len = atomic_read(&ps_data->firlength);

        printk(KERN_INFO "%s: len = %2d, idx = %2d\n", __func__, len, ps_data->fir.idx);
        printk(KERN_INFO "%s: sum = %5d, ave = %5d\n", __func__, ps_data->fir.sum, ps_data->fir.sum/len);

        return scnprintf(buf, PAGE_SIZE, "%d\n", len);
}


static ssize_t stk_als_firlen_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    uint64_t value = 0;
        int ret;
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        ret = kstrtoull(buf, 10, &value);
        if(ret < 0)
        {
                printk(KERN_ERR "%s:kstrtoull failed, ret=0x%x\n", __func__, ret);
                return ret;
        }

        if(value > MAX_FIR_LEN)
        {
                printk(KERN_ERR "%s: firlen exceed maximum filter length\n", __func__);
        }
        else if (value < 1)
        {
                atomic_set(&ps_data->firlength, 1);
                memset(&ps_data->fir, 0x00, sizeof(ps_data->fir));
        }
        else
        { 
                atomic_set(&ps_data->firlength, value);
                memset(&ps_data->fir, 0x00, sizeof(ps_data->fir));
        }
        return size;
}
#endif  /* #ifdef STK_ALS_FIR */

#ifdef STK_GES
static ssize_t stk_ges_code_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
    int ret;
        unsigned int gest0 = 0, gest1 = 0, gest2 = 0;

        if(!ps_data->ges_enabled)
                return 0;

    ret = stk3x1x_get_ges_reading(ps_data, &gest0, &gest1, &gest2);
        if(ret < 0)
                return ret;
        else if(ret == 0xFFFF)
                atomic_set(&ps_data->gesture2, 0);

    return scnprintf(buf, PAGE_SIZE, "%5d,%5d,%5d\n", gest0, gest1, atomic_read(&ps_data->gesture2));
}

static ssize_t stk_ges_code_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        uint8_t ges;
        unsigned long value = 0;
        int ret;

        ret = kstrtoul(buf, 16, &value);
        if(ret < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=%d\n", __func__, ret);
                return ret;
        }

        switch(value)
        {
        case 3:
                //printk(KERN_INFO "%s: ges input event, not detected\n",__func__);
        case 0:
                return size;
        case 1:
                ges = KEY_PAGEUP;
                atomic_set(&ps_data->gesture2, 0);
                printk(KERN_INFO "%s: ges input event >>>\n",__func__);
                break;
        case 2:
                ges = KEY_PAGEDOWN;
                atomic_set(&ps_data->gesture2, 0);
                printk(KERN_INFO "%s: ges input event <<<\n",__func__);
                break;
        case 32:
                ges = KEY_VOLUMEDOWN;
                printk(KERN_INFO "%s: ges input event near\n",__func__);
                break;
        case 48:
                ges = KEY_VOLUMEUP;
                printk(KERN_INFO "%s: ges input event far\n",__func__);
                break;
        default:
                printk(KERN_ERR "%s, invalid value %d\n", __func__, (int)value);
                return -EINVAL;
        }

        input_report_key(ps_data->ges_input_dev, ges, 1);
        input_report_key(ps_data->ges_input_dev, ges, 0);
        input_sync(ps_data->ges_input_dev);
    return size;
}

static ssize_t stk_ges_poll_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        int len = 0, ii = 0, jj = 0;

        while(stk_ges_op[ii].ops[0] != 0)
        {
                len += scnprintf(buf + len, PAGE_SIZE - len, "%x ", ii);
                for(jj=0;jj<4;jj++)
                        len += scnprintf(buf + len, PAGE_SIZE - len, "%x ", stk_ges_op[ii].ops[jj]);
                len += scnprintf(buf + len, PAGE_SIZE - len, "\n");
                ii++;
        }
        return len;
}


static ssize_t stk_ges_poll_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        int32_t ret, i = 0, index = 0;
        char *token;
        unsigned long value = 0;

        while(buf != '\0')
        {
                token = strsep((char **)&buf, " ");
                if((ret = kstrtoul(token, 16, &value)) < 0)
                {
                        printk(KERN_ERR "%s:kstrtoul failed, ret=0x%x\n", __func__, ret);
                        return ret;
                }

                if(i == 0)
                {
                        if(value >= 10)
                        {
                                memset(stk_ges_op, 0, sizeof(stk_ges_op));
                                break;
                        }
                        else
                                index = value;
                }
                else
                {
                        stk_ges_op[index].ops[i-1] = value;
                }
                i++;
                if(i == 5)
                        break;
        }
        if(i != 5)
        {
                printk(KERN_ERR "%s: invalid length(%d)\n", __func__, i);
                memset(&(stk_ges_op[index]), 0, sizeof(union stk_ges_operation));
        }
        return size;
}

static ssize_t stk_ges_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        unsigned long value = 0;
        int ret;

        ret = kstrtoul(buf, 10, &value);
        if(ret < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=%d\n", __func__, ret);
                return ret;
        }
    printk(KERN_INFO "%s: Enable GES : %d\n", __func__, (int)value);

        switch(value)
        {
        case 0:
                mutex_lock(&ps_data->io_lock);
                if(ps_data->ges_enabled == 1)
                        stk3x1x_enable_ges(ps_data, 0, 1);
                else
                        stk3x1x_enable_ges(ps_data, 0, 2);
                mutex_unlock(&ps_data->io_lock);
                break;
        case 1:
                mutex_lock(&ps_data->io_lock);
                stk3x1x_enable_ges(ps_data, 1, 1);
                mutex_unlock(&ps_data->io_lock);
                break;
        case 2:
                mutex_lock(&ps_data->io_lock);
                stk3x1x_enable_ges(ps_data, 1, 2);
                mutex_unlock(&ps_data->io_lock);
                break;
        default:
                printk(KERN_ERR "%s, invalid value %d\n", __func__, *buf);
                return -EINVAL;
                break;
        }

    return size;
}

static ssize_t stk_ges_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        return scnprintf(buf, PAGE_SIZE, "%d\n", ps_data->ges_enabled);
}

#endif  /* #ifdef STK_GES */

static ssize_t stk_ps_code_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
    uint32_t reading;
    reading = stk3x1x_get_ps_reading(ps_data);
    return scnprintf(buf, PAGE_SIZE, "%d\n", reading);
}

static ssize_t stk_ps_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int32_t ret;
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);

        ret = stk3x1x_get_state(ps_data);
        if(ret < 0)
                return ret;
    ret = (ret & STK_STATE_EN_PS_MASK)?1:0;

        return scnprintf(buf, PAGE_SIZE, "%d\n", ret);
}

static ssize_t stk_ps_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        uint8_t en;
        if (sysfs_streq(buf, "1"))
                en = 1;
        else if (sysfs_streq(buf, "0"))
                en = 0;
        else 
        {
                printk(KERN_ERR "%s, invalid value %d\n", __func__, *buf);
                return -EINVAL;
        }
    printk(KERN_INFO "%s: Enable PS : %d\n", __func__, en);
    mutex_lock(&ps_data->io_lock);
    stk3x1x_enable_ps(ps_data, en, 1);
    mutex_unlock(&ps_data->io_lock);
    return size;
}

static ssize_t stk_ps_enable_aso_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int32_t ret;
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);

    ret = stk3x1x_i2c_smbus_read_byte_data(ps_data->client,STK_STATE_REG);
    ret = (ret & STK_STATE_EN_ASO_MASK)?1:0;

        return scnprintf(buf, PAGE_SIZE, "%d\n", ret);
}

static ssize_t stk_ps_enable_aso_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        uint8_t en;
    int32_t ret;
        uint8_t w_state_reg;

        if (sysfs_streq(buf, "1"))
                en = 1;
        else if (sysfs_streq(buf, "0"))
                en = 0;
        else 
        {
                printk(KERN_ERR "%s, invalid value %d\n", __func__, *buf);
                return -EINVAL;
        }
    printk(KERN_INFO "%s: Enable PS ASO : %d\n", __func__, en);
    
    ret = stk3x1x_i2c_smbus_read_byte_data(ps_data->client, STK_STATE_REG);
    if (ret < 0)
    {
        printk(KERN_ERR "%s: write i2c error\n", __func__);
                return ret;
    }
        w_state_reg = (uint8_t)(ret & (~STK_STATE_EN_ASO_MASK)); 
        if(en)
                w_state_reg |= STK_STATE_EN_ASO_MASK;

    ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_STATE_REG, w_state_reg);
    if (ret < 0)
        {
                printk(KERN_ERR "%s: write i2c error\n", __func__);
                return ret;
        }

        return size;
}


static ssize_t stk_ps_offset_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
    int32_t word_data;
        unsigned char value[2];
        int ret;

        ret = stk3x1x_i2c_read_data(ps_data->client, STK_DATA1_OFFSET_REG, 2, &value[0]);
        if(ret < 0)
        {
                printk(KERN_ERR "%s fail, ret=0x%x", __func__, ret);
                return ret;
        }
        word_data = (value[0]<<8) | value[1];

        return scnprintf(buf, PAGE_SIZE, "%d\n", word_data);
}
 
static ssize_t stk_ps_offset_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        unsigned long offset = 0;
        int ret;
        unsigned char val[2];

        ret = kstrtoul(buf, 10, &offset);
        if(ret < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=0x%x\n", __func__, ret);
                return ret;
        }
        if(offset > 65535)
        {
                printk(KERN_ERR "%s: invalid value, offset=%ld\n", __func__, offset);
                return -EINVAL;
        }

        val[0] = (offset & 0xFF00) >> 8;
        val[1] = offset & 0x00FF;
        ret = stk3x1x_i2c_write_data(ps_data->client, STK_DATA1_OFFSET_REG, 2, val);
        if(ret < 0)
        {
                printk(KERN_ERR "%s: write i2c error\n", __func__);
                return ret;
        }

        return size;
}


static ssize_t stk_ps_distance_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
    int32_t dist=1, ret;

    ret = stk3x1x_get_flag(ps_data);
        if(ret < 0)
                return ret;
    dist = (ret & STK_FLG_NF_MASK)?1:0;

    ps_data->ps_distance_last = dist;
        input_report_abs(ps_data->ps_input_dev, ABS_DISTANCE, dist);
        input_sync(ps_data->ps_input_dev);
        //wake_lock_timeout(&ps_data->ps_wakelock, 3*HZ);
        printk(KERN_INFO "%s: ps input event %d cm\n",__func__, dist);
    return scnprintf(buf, PAGE_SIZE, "%d\n", dist);
}


static ssize_t stk_ps_distance_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        unsigned long value = 0;
        int ret;
        ret = kstrtoul(buf, 10, &value);
        if(ret < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=0x%x\n", __func__, ret);
                return ret;
        }
    ps_data->ps_distance_last = value;
        input_report_abs(ps_data->ps_input_dev, ABS_DISTANCE, value);
        input_sync(ps_data->ps_input_dev);
      //  wake_lock_timeout(&ps_data->ps_wakelock, 3*HZ);
        printk(KERN_INFO "%s: ps input event %ld cm\n",__func__, value);
    return size;
}


static ssize_t stk_ps_code_thd_l_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int32_t ps_thd_l1_reg, ps_thd_l2_reg;
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
    ps_thd_l1_reg = stk3x1x_i2c_smbus_read_byte_data(ps_data->client,STK_THDL1_PS_REG);
    if(ps_thd_l1_reg < 0)
        {
                printk(KERN_ERR "%s fail, err=0x%x", __func__, ps_thd_l1_reg);
                return -EINVAL;
        }
    ps_thd_l2_reg = stk3x1x_i2c_smbus_read_byte_data(ps_data->client,STK_THDL2_PS_REG);
    if(ps_thd_l2_reg < 0)
        {
                printk(KERN_ERR "%s fail, err=0x%x", __func__, ps_thd_l2_reg);
                return -EINVAL;
        }
        ps_thd_l1_reg = ps_thd_l1_reg<<8 | ps_thd_l2_reg;
    return scnprintf(buf, PAGE_SIZE, "%d\n", ps_thd_l1_reg);
}


static ssize_t stk_ps_code_thd_l_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        unsigned long value = 0;
        int ret;
        ret = kstrtoul(buf, 10, &value);
        if(ret < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=0x%x\n", __func__, ret);
                return ret;         
        }
    stk3x1x_set_ps_thd_l(ps_data, value);
    return size;
}

static ssize_t stk_ps_code_thd_h_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int32_t ps_thd_h1_reg, ps_thd_h2_reg;
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
    ps_thd_h1_reg = stk3x1x_i2c_smbus_read_byte_data(ps_data->client,STK_THDH1_PS_REG);
    if(ps_thd_h1_reg < 0)
        {
                printk(KERN_ERR "%s fail, err=0x%x", __func__, ps_thd_h1_reg);
                return -EINVAL;
        }
    ps_thd_h2_reg = stk3x1x_i2c_smbus_read_byte_data(ps_data->client,STK_THDH2_PS_REG);
    if(ps_thd_h2_reg < 0)
        {
                printk(KERN_ERR "%s fail, err=0x%x", __func__, ps_thd_h2_reg);
                return -EINVAL;
        }
        ps_thd_h1_reg = ps_thd_h1_reg<<8 | ps_thd_h2_reg;
    return scnprintf(buf, PAGE_SIZE, "%d\n", ps_thd_h1_reg);
}


static ssize_t stk_ps_code_thd_h_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        unsigned long value = 0;
        int ret;
        ret = kstrtoul(buf, 10, &value);
        if(ret < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=0x%x\n", __func__, ret);
                return ret;         
        }
    stk3x1x_set_ps_thd_h(ps_data, value);
    return size;
}


static ssize_t stk_all_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int32_t ps_reg[0x22];
        uint8_t cnt;
        int len = 0;
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);

        for(cnt=0;cnt<0x20;cnt++)
        {
                ps_reg[cnt] = stk3x1x_i2c_smbus_read_byte_data(ps_data->client, (cnt));
                if(ps_reg[cnt] < 0)
                {
                        printk(KERN_ERR "%s fail, ret=%d", __func__, ps_reg[cnt]);
                        return -EINVAL;
                }
                else
                {
                        printk(KERN_INFO "reg[0x%2X]=0x%2X\n", cnt, ps_reg[cnt]);
                        len += scnprintf(buf+len, PAGE_SIZE-len, "[%2X]%2X,", cnt, ps_reg[cnt]);
                }
        }
        ps_reg[cnt] = stk3x1x_i2c_smbus_read_byte_data(ps_data->client, STK_PDT_ID_REG);
        if(ps_reg[cnt] < 0)
        {
                printk( KERN_ERR "%s fail, ret=%d", __func__, ps_reg[cnt]);
                return -EINVAL;
        }
        printk( KERN_INFO "reg[0x%x]=0x%2X\n", STK_PDT_ID_REG, ps_reg[cnt]);
        cnt++;
        ps_reg[cnt] = stk3x1x_i2c_smbus_read_byte_data(ps_data->client, STK_RSRVD_REG);
        if(ps_reg[cnt] < 0)
        {
                printk( KERN_ERR "%s fail, ret=%d", __func__, ps_reg[cnt]);
                return -EINVAL;
        }
        printk( KERN_INFO "reg[0x%x]=0x%2X\n", STK_RSRVD_REG, ps_reg[cnt]);
        len += scnprintf(buf+len, PAGE_SIZE-len, "[%2X]%2X,[%2X]%2X\n", cnt-1, ps_reg[cnt-1], cnt, ps_reg[cnt]);
        return len;
/*
    return scnprintf(buf, PAGE_SIZE, "[0]%2X [1]%2X [2]%2X [3]%2X [4]%2X [5]%2X [6/7 HTHD]%2X,%2X [8/9 LTHD]%2X, %2X [A]%2X [B]%2X [C]%2X [D]%2X [E/F Aoff]%2X,%2X,[10]%2X [11/12 PS]%2X,%2X [13]%2X [14]%2X [15/16 Foff]%2X,%2X [17]%2X [18]%2X [3E]%2X [3F]%2X\n", 
                ps_reg[0], ps_reg[1], ps_reg[2], ps_reg[3], ps_reg[4], ps_reg[5], ps_reg[6], ps_reg[7], ps_reg[8], 
                ps_reg[9], ps_reg[10], ps_reg[11], ps_reg[12], ps_reg[13], ps_reg[14], ps_reg[15], ps_reg[16], ps_reg[17], 
                ps_reg[18], ps_reg[19], ps_reg[20], ps_reg[21], ps_reg[22], ps_reg[23], ps_reg[24], ps_reg[25], ps_reg[26]);
                */
}

static ssize_t stk_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    int32_t ps_reg[27];
        uint8_t cnt;
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        for(cnt=0;cnt<25;cnt++)
        {
                ps_reg[cnt] = stk3x1x_i2c_smbus_read_byte_data(ps_data->client, (cnt));
                if(ps_reg[cnt] < 0)
                {
                        printk(KERN_ERR "%s fail, ret=%d", __func__, ps_reg[cnt]);
                        return -EINVAL;
                }
                else
                {
                        printk(KERN_INFO "reg[0x%2X]=0x%2X\n", cnt, ps_reg[cnt]);
                }
        }
        ps_reg[cnt] = stk3x1x_i2c_smbus_read_byte_data(ps_data->client, STK_PDT_ID_REG);
        if(ps_reg[cnt] < 0)
        {
                printk( KERN_ERR "%s fail, ret=%d", __func__, ps_reg[cnt]);
                return -EINVAL;
        }
        printk( KERN_INFO "reg[0x%x]=0x%2X\n", STK_PDT_ID_REG, ps_reg[cnt]);
        cnt++;
        ps_reg[cnt] = stk3x1x_i2c_smbus_read_byte_data(ps_data->client, STK_RSRVD_REG);
        if(ps_reg[cnt] < 0)
        {
                printk( KERN_ERR "%s fail, ret=%d", __func__, ps_reg[cnt]);
                return -EINVAL;
        }
        printk( KERN_INFO "reg[0x%x]=0x%2X\n", STK_RSRVD_REG, ps_reg[cnt]);

    return scnprintf(buf, PAGE_SIZE, "[PS=%2X] [ALS=%2X] [WAIT=0x%4Xms] [EN_ASO=%2X] [EN_AK=%2X] [NEAR/FAR=%2X] [FLAG_OUI=%2X] [FLAG_PSINT=%2X] [FLAG_ALSINT=%2X]\n", 
                ps_reg[0]&0x01,(ps_reg[0]&0x02)>>1,((ps_reg[0]&0x04)>>2)*ps_reg[5]*6,(ps_reg[0]&0x20)>>5,
                (ps_reg[0]&0x40)>>6,ps_reg[16]&0x01,(ps_reg[16]&0x04)>>2,(ps_reg[16]&0x10)>>4,(ps_reg[16]&0x20)>>5);
}

static ssize_t stk_recv_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        return scnprintf(buf, PAGE_SIZE, "0x%04X\n", atomic_read(&ps_data->recv_reg));     
}


static ssize_t stk_recv_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    unsigned long value = 0;
        int ret;
        int32_t recv_data;
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);

        if((ret = kstrtoul(buf, 16, &value)) < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=0x%x\n", __func__, ret);
                return ret;
        }
        recv_data = stk3x1x_i2c_smbus_read_byte_data(ps_data->client,value);
//      printk("%s: reg 0x%x=0x%x\n", __func__, (int)value, recv_data);
        atomic_set(&ps_data->recv_reg, recv_data);
        return size;
}


static ssize_t stk_send_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        return 0;
}


static ssize_t stk_send_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        int addr, cmd;
        int32_t ret, i;
        char *token[10];
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);

        for (i = 0; i < 2; i++)
                token[i] = strsep((char **)&buf, " ");
        if((ret = kstrtoul(token[0], 16, (unsigned long *)&(addr))) < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=0x%x\n", __func__, ret);
                return ret;
        }
        if((ret = kstrtoul(token[1], 16, (unsigned long *)&(cmd))) < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=0x%x\n", __func__, ret);
                return ret;
        }
        printk(KERN_INFO "%s: write reg 0x%x=0x%x\n", __func__, addr, cmd);

        ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, (unsigned char)addr, (unsigned char)cmd);
        if (0 != ret)
        {
                printk(KERN_ERR "%s: stk3x1x_i2c_smbus_write_byte_data fail\n", __func__);
                return ret;
        }

        return size;
}

#ifdef STK_TUNE0

static ssize_t stk_ps_cali_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        int32_t word_data;
        unsigned char value[2];
        int ret;

        ret = stk3x1x_i2c_read_data(ps_data->client, 0x20, 2, &value[0]);
        if(ret < 0)
        {
                printk(KERN_ERR "%s fail, ret=0x%x", __func__, ret);
                return ret;
        }
        word_data = (value[0]<<8) | value[1];

        ret = stk3x1x_i2c_read_data(ps_data->client, 0x22, 2, &value[0]);
        if(ret < 0)
        {
                printk(KERN_ERR "%s fail, ret=0x%x", __func__, ret);
                return ret;
        }
        word_data += ((value[0]<<8) | value[1]);

        printk("%s: psi_set=%d, psa=%d,psi=%d, word_data=%d\n", __func__, 
                ps_data->psi_set, ps_data->psa, ps_data->psi, word_data);
#ifdef CALI_PS_EVERY_TIME
        printk("%s: boot HT=%d, LT=%d\n", __func__, ps_data->ps_high_thd_boot, ps_data->ps_low_thd_boot);
#endif
        return 0;
}

static ssize_t stk_ps_maxdiff_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
    unsigned long value = 0;
        int ret;

        if((ret = kstrtoul(buf, 10, &value)) < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=0x%x\n", __func__, ret);
                return ret;
        }
        ps_data->stk_max_min_diff = (int) value;
        return size;
}


static ssize_t stk_ps_maxdiff_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        return scnprintf(buf, PAGE_SIZE, "%d\n", ps_data->stk_max_min_diff);     
}

static ssize_t stk_ps_ltnct_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
    unsigned long value = 0;
        int ret;

        if((ret = kstrtoul(buf, 10, &value)) < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=0x%x\n", __func__, ret);
                return ret;
        }
        ps_data->stk_lt_n_ct = (int) value;
        return size;
}

static ssize_t stk_ps_ltnct_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        return scnprintf(buf, PAGE_SIZE, "%d\n", ps_data->stk_lt_n_ct);     
}

static ssize_t stk_ps_htnct_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
    unsigned long value = 0;
        int ret;

        if((ret = kstrtoul(buf, 10, &value)) < 0)
        {
                printk(KERN_ERR "%s:kstrtoul failed, ret=0x%x\n", __func__, ret);
                return ret;
        }
        ps_data->stk_ht_n_ct = (int) value;
        return size;
}

static ssize_t stk_ps_htnct_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        struct stk3x1x_data *ps_data =  dev_get_drvdata(dev);
        return scnprintf(buf, PAGE_SIZE, "%d\n", ps_data->stk_ht_n_ct);     
}
#endif  /* #ifdef STK_TUNE0 */

static struct device_attribute als_enable_attribute = __ATTR(enable,0664,stk_als_enable_show,stk_als_enable_store);
static struct device_attribute als_lux_attribute = __ATTR(lux,0664,stk_als_lux_show,stk_als_lux_store);
static struct device_attribute als_code_attribute = __ATTR(code, 0664, stk_als_code_show, NULL);
static struct device_attribute als_transmittance_attribute = __ATTR(transmittance,0664,stk_als_transmittance_show,stk_als_transmittance_store);
static struct device_attribute als_poll_delay_attribute = __ATTR(delay,0664,stk_als_delay_show,stk_als_delay_store);
static struct device_attribute als_ir_code_attribute = __ATTR(ircode,0664,stk_als_ir_code_show,NULL);
#ifdef STK_ALS_FIR
static struct device_attribute als_firlen_attribute = __ATTR(firlen,0664,stk_als_firlen_show,stk_als_firlen_store);
#endif

static struct attribute *stk_als_attrs [] =
{
        &als_enable_attribute.attr,
    &als_lux_attribute.attr,
    &als_code_attribute.attr,
    &als_transmittance_attribute.attr,
        &als_poll_delay_attribute.attr,
        &als_ir_code_attribute.attr,
#ifdef STK_ALS_FIR
        &als_firlen_attribute.attr,
#endif
    NULL
};

static struct attribute_group stk_als_attribute_group = {
#ifndef QUALCOMM_PLATFORM
        .name = "driver",
#endif
        .attrs = stk_als_attrs,
};

#ifdef STK_GES
static struct device_attribute ges_enable_attribute = __ATTR(enable,0664,stk_ges_enable_show,stk_ges_enable_store);
static struct device_attribute ges_code_attribute = __ATTR(code, 0664, stk_ges_code_show, stk_ges_code_store);
static struct device_attribute ges_poll_attribute = __ATTR(poll, 0664, stk_ges_poll_show, stk_ges_poll_store);
static struct device_attribute ges_recv_attribute = __ATTR(recv,0664,stk_recv_show,stk_recv_store);
static struct device_attribute ges_send_attribute = __ATTR(send,0664,stk_send_show, stk_send_store);

static struct attribute *stk_ges_attrs [] =
{
    &ges_enable_attribute.attr,
    &ges_code_attribute.attr,
    &ges_poll_attribute.attr,
        &ges_recv_attribute.attr,
        &ges_send_attribute.attr,
    NULL
};

static struct attribute_group stk_ges_attribute_group = 
{
        .name = "driver",
        .attrs = stk_ges_attrs,
};
#endif  /* #ifdef STK_GES */

static struct device_attribute ps_enable_attribute = __ATTR(enable,0664,stk_ps_enable_show,stk_ps_enable_store);
static struct device_attribute ps_enable_aso_attribute = __ATTR(enableaso,0664,stk_ps_enable_aso_show,stk_ps_enable_aso_store);
static struct device_attribute ps_distance_attribute = __ATTR(distance,0664,stk_ps_distance_show, stk_ps_distance_store);
static struct device_attribute ps_offset_attribute = __ATTR(offset,0664,stk_ps_offset_show, stk_ps_offset_store);
static struct device_attribute ps_code_attribute = __ATTR(code, 0444, stk_ps_code_show, NULL);
static struct device_attribute ps_code_thd_l_attribute = __ATTR(codethdl,0664,stk_ps_code_thd_l_show,stk_ps_code_thd_l_store);
static struct device_attribute ps_code_thd_h_attribute = __ATTR(codethdh,0664,stk_ps_code_thd_h_show,stk_ps_code_thd_h_store);
static struct device_attribute ps_recv_attribute = __ATTR(recv,0664,stk_recv_show,stk_recv_store);
static struct device_attribute ps_send_attribute = __ATTR(send,0664,stk_send_show, stk_send_store);
static struct device_attribute all_reg_attribute = __ATTR(allreg, 0664, stk_all_reg_show, NULL);
static struct device_attribute status_attribute = __ATTR(status, 0664, stk_status_show, NULL);
#ifdef STK_TUNE0
static struct device_attribute ps_cali_attribute = __ATTR(cali,0444,stk_ps_cali_show, NULL);
static struct device_attribute ps_maxdiff_attribute = __ATTR(maxdiff,0664,stk_ps_maxdiff_show, stk_ps_maxdiff_store);
static struct device_attribute ps_ltnct_attribute = __ATTR(ltnct,0664,stk_ps_ltnct_show, stk_ps_ltnct_store);
static struct device_attribute ps_htnct_attribute = __ATTR(htnct,0664,stk_ps_htnct_show, stk_ps_htnct_store);
#endif

//static DRIVER_ATTR(stk_allreg,  S_IWUSR | S_IRUGO, stk_all_reg_show,   NULL);

static struct attribute *stk_ps_attrs [] =
{
    &ps_enable_attribute.attr,
    &ps_enable_aso_attribute.attr,
    &ps_distance_attribute.attr,
        &ps_offset_attribute.attr,
    &ps_code_attribute.attr,
        &ps_code_thd_l_attribute.attr,
        &ps_code_thd_h_attribute.attr,
        &ps_recv_attribute.attr,
        &ps_send_attribute.attr,
        &all_reg_attribute.attr,
        &status_attribute.attr,
#ifdef STK_TUNE0
        &ps_cali_attribute.attr,
        &ps_maxdiff_attribute.attr,
        &ps_ltnct_attribute.attr,
        &ps_htnct_attribute.attr,
#endif
    NULL
};

static struct attribute_group stk_ps_attribute_group = {
#ifndef QUALCOMM_PLATFORM
        .name = "driver",
#endif
        .attrs = stk_ps_attrs,
};

static int stk_ps_val(struct stk3x1x_data *ps_data)
{
        int mode;
        int32_t word_data, lii;
        unsigned char value[4];
        int ret;

        ret = stk3x1x_i2c_read_data(ps_data->client, 0x20, 4, value);
        if(ret < 0)
        {
                printk(KERN_ERR "%s fail, ret=0x%x", __func__, ret);
                return ret;
        }
        word_data = (value[0]<<8) | value[1];
        word_data += ((value[2]<<8) | value[3]);

        mode = (ps_data->psctrl_reg) & 0x3F;
        if(mode == 0x30)
                lii = 100;
        else if (mode == 0x31)
                lii = 200;
        else if (mode == 0x32)
                lii = 400;
        else if (mode == 0x33)
                lii = 800;
        else
        {
                printk(KERN_ERR "%s: unsupported PS_IT(0x%x)\n", __func__, mode);
                return -1;
        }

        if(word_data > lii)
        {
                printk(KERN_INFO "%s: word_data=%d, lii=%d\n", __func__, word_data, lii);
                return 0xFFFF;
        }
        return 0;
}

#ifdef STK_TUNE0

static int stk_ps_tune_zero_final(struct stk3x1x_data *ps_data)
{
        int ret;

        ps_data->tune_zero_init_proc = false;
        ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_INT_REG, ps_data->int_reg);
        if (ret < 0)
        {
                printk(KERN_ERR "%s: write i2c error\n", __func__);
                return ret;
        }

        ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_STATE_REG, 0);
        if (ret < 0)
        {
                printk(KERN_ERR "%s: write i2c error\n", __func__);
                return ret;
        }

        if(ps_data->data_count == -1)
        {
                printk(KERN_INFO "%s: exceed limit\n", __func__);
                hrtimer_cancel(&ps_data->ps_tune0_timer);
                return 0;
        }

        ps_data->psa = ps_data->ps_stat_data[0];
        ps_data->psi = ps_data->ps_stat_data[2];

#ifdef CALI_PS_EVERY_TIME
        ps_data->ps_high_thd_boot = ps_data->ps_stat_data[1] + ps_data->stk_ht_n_ct*3;
        ps_data->ps_low_thd_boot = ps_data->ps_stat_data[1] + ps_data->stk_lt_n_ct*3;
        ps_data->ps_thd_h = ps_data->ps_high_thd_boot ;
        ps_data->ps_thd_l = ps_data->ps_low_thd_boot ;
#else
        ps_data->ps_thd_h = ps_data->ps_stat_data[1] + ps_data->stk_ht_n_ct;
        ps_data->ps_thd_l = ps_data->ps_stat_data[1] + ps_data->stk_lt_n_ct;
#endif
        stk3x1x_set_ps_thd_h(ps_data, ps_data->ps_thd_h);
        stk3x1x_set_ps_thd_l(ps_data, ps_data->ps_thd_l);
        printk(KERN_INFO "%s: set HT=%d,LT=%d\n", __func__, ps_data->ps_thd_h,  ps_data->ps_thd_l);
        hrtimer_cancel(&ps_data->ps_tune0_timer);
        return 0;
}

static int32_t stk_tune_zero_get_ps_data(struct stk3x1x_data *ps_data)
{
        uint32_t ps_adc;
        int ret;

        ret = stk_ps_val(ps_data);
        if(ret == 0xFFFF)
        {
                ps_data->data_count = -1;
                stk_ps_tune_zero_final(ps_data);
                return 0;
        }

        ps_adc = stk3x1x_get_ps_reading(ps_data);
        printk(KERN_INFO "%s: ps_adc #%d=%d\n", __func__, ps_data->data_count, ps_adc);
        if(ps_adc < 0)
                return ps_adc;

        ps_data->ps_stat_data[1]  +=  ps_adc;
        if(ps_adc > ps_data->ps_stat_data[0])
                ps_data->ps_stat_data[0] = ps_adc;
        if(ps_adc < ps_data->ps_stat_data[2])
                ps_data->ps_stat_data[2] = ps_adc;
        ps_data->data_count++;

        if(ps_data->data_count == 5)
        {
                ps_data->ps_stat_data[1]  /= ps_data->data_count;
                stk_ps_tune_zero_final(ps_data);
        }

        return 0;
}
static int stk_ps_tune_zero_init(struct stk3x1x_data *ps_data)
{
        int32_t ret = 0;
        uint8_t w_state_reg;

        ps_data->psi_set = 0;
        ps_data->ps_stat_data[0] = 0;
        ps_data->ps_stat_data[2] = 9999;
        ps_data->ps_stat_data[1] = 0;
        ps_data->data_count = 0;

        ps_data->tune_zero_init_proc = true;
        ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_INT_REG, 0);
        if (ret < 0)
        {
                printk(KERN_ERR "%s: write i2c error\n", __func__);
                return ret;
        }

        w_state_reg = (STK_STATE_EN_PS_MASK | STK_STATE_EN_WAIT_MASK);
        ret = stk3x1x_i2c_smbus_write_byte_data(ps_data->client, STK_STATE_REG, w_state_reg);
        if (ret < 0)
        {
                printk(KERN_ERR "%s: write i2c error\n", __func__);
                return ret;
        }
        hrtimer_start(&ps_data->ps_tune0_timer, ps_data->ps_tune0_delay, HRTIMER_MODE_REL);

        return 0;
}
static int stk_ps_tune_zero_func_fae(struct stk3x1x_data *ps_data)
{
        int32_t word_data;
        int ret, diff;
        unsigned char value[2];

#ifdef CALI_PS_EVERY_TIME
        if(!(ps_data->ps_enabled))
#else
        if(ps_data->psi_set || !(ps_data->ps_enabled))
#endif
        {
                return 0;
        }

        ret = stk3x1x_get_flag(ps_data);
        if(ret < 0)
                return ret;
        if(!(ret&STK_FLG_PSDR_MASK))
        {
                //printk(KERN_INFO "%s: ps data is not ready yet\n", __func__);
                return 0;
        }

        ret = stk_ps_val(ps_data);
        if(ret == 0)
        {
                ret = stk3x1x_i2c_read_data(ps_data->client, 0x11, 2, &value[0]);
                if(ret < 0)
                {
                        printk(KERN_ERR "%s fail, ret=0x%x", __func__, ret);
                        return ret;
                }
                word_data = (value[0]<<8) | value[1];
                //printk(KERN_INFO "%s: word_data=%d\n", __func__, word_data);

                if(word_data == 0)
                {
                        //printk(KERN_ERR "%s: incorrect word data (0)\n", __func__);
                        return 0xFFFF;
                }

                if(word_data > ps_data->psa)
                {
                        ps_data->psa = word_data;
                        printk(KERN_INFO "%s: update psa: psa=%d,psi=%d\n", __func__, ps_data->psa, ps_data->psi);
                }
                if(word_data < ps_data->psi)
                {
                        ps_data->psi = word_data;
                        printk(KERN_INFO "%s: update psi: psa=%d,psi=%d\n", __func__, ps_data->psa, ps_data->psi);
                }
        }
        diff = ps_data->psa - ps_data->psi;
        if(diff > ps_data->stk_max_min_diff)
        {
                ps_data->psi_set = ps_data->psi;
                ps_data->ps_thd_h = ps_data->psi + ps_data->stk_ht_n_ct;
                ps_data->ps_thd_l = ps_data->psi + ps_data->stk_lt_n_ct;


#ifdef CALI_PS_EVERY_TIME
                if(ps_data->ps_thd_h > ps_data->ps_high_thd_boot)
                {
                        ps_data->ps_high_thd_boot = ps_data->ps_thd_h;
                        ps_data->ps_low_thd_boot = ps_data->ps_thd_l;
                        printk(KERN_INFO "%s: update boot HT=%d, LT=%d\n", __func__, ps_data->ps_high_thd_boot, ps_data->ps_low_thd_boot);
                }
#endif

                stk3x1x_set_ps_thd_h(ps_data, ps_data->ps_thd_h);
                stk3x1x_set_ps_thd_l(ps_data, ps_data->ps_thd_l);
#ifdef STK_DEBUG_PRINTF
                printk(KERN_INFO "%s: FAE tune0 psa-psi(%d) > STK_DIFF found\n", __func__, diff);
#endif
                hrtimer_cancel(&ps_data->ps_tune0_timer);
        }

        return 0;
}

static void stk_ps_tune0_work_func(struct work_struct *work)
{
        struct stk3x1x_data *ps_data = container_of(work, struct stk3x1x_data, stk_ps_tune0_work);
        if(ps_data->tune_zero_init_proc)
                stk_tune_zero_get_ps_data(ps_data);
        else
                stk_ps_tune_zero_func_fae(ps_data);
        return;
}


static enum hrtimer_restart stk_ps_tune0_timer_func(struct hrtimer *timer)
{
        struct stk3x1x_data *ps_data = container_of(timer, struct stk3x1x_data, ps_tune0_timer);
        queue_work(ps_data->stk_ps_tune0_wq, &ps_data->stk_ps_tune0_work);
        hrtimer_forward_now(&ps_data->ps_tune0_timer, ps_data->ps_tune0_delay);
        return HRTIMER_RESTART;
}
#endif

#ifdef STK_POLL_ALS
static enum hrtimer_restart stk_als_timer_func(struct hrtimer *timer)
{
        struct stk3x1x_data *ps_data = container_of(timer, struct stk3x1x_data, als_timer);
        queue_work(ps_data->stk_als_wq, &ps_data->stk_als_work);
        hrtimer_forward_now(&ps_data->als_timer, ps_data->als_poll_delay);
        return HRTIMER_RESTART;

}

static void stk_als_poll_work_func(struct work_struct *work)
{
        struct stk3x1x_data *ps_data = container_of(work, struct stk3x1x_data, stk_als_work);
        int32_t reading, reading_lux, als_comperator, flag_reg;
        #ifdef STK_IRS
        int ret;
        #endif
#ifdef STK_GES
        if(ps_data->ges_enabled)
        {
                input_report_abs(ps_data->als_input_dev, ABS_MISC, ps_data->als_lux_last);
                input_sync(ps_data->als_input_dev);
#ifdef STK_DEBUG_PRINTF
                printk(KERN_INFO "%s: ges_enabled=1, als input event %d lux\n",__func__, ps_data->als_lux_last);
#endif
        }
#endif

        flag_reg = stk3x1x_get_flag(ps_data);
        if(flag_reg < 0)
                return;

        #ifdef STK_IRS
        if(ps_data->als_data_index < 60000)
                ps_data->als_data_index++;
        else
                ps_data->als_data_index = 0;

        if(     ps_data->als_data_index % 10 == 0)
        {
                if(ps_data->ps_distance_last != 0)
                {
                        ret = stk3x1x_get_ir_reading(ps_data);
                        if(ret > 0)
                                ps_data->ir_code = ret;
                }
                return;
        }
        #endif

        if(!(flag_reg&STK_FLG_ALSDR_MASK))
        {
                //printk(KERN_INFO "%s: als is not ready\n", __func__);
                return;
        }

        reading = stk3x1x_get_als_reading(ps_data);
        if(reading < 0)
        {
                return;
        }

        if(ps_data->ir_code)
        {
                ps_data->als_correct_factor = 1000;
                if(reading < STK_IRC_MAX_ALS_CODE && reading > STK_IRC_MIN_ALS_CODE && 
                        ps_data->ir_code > STK_IRC_MIN_IR_CODE)
                {
                        als_comperator = reading * STK_IRC_ALS_NUMERA / STK_IRC_ALS_DENOMI;
                        if(ps_data->ir_code > als_comperator)
                                ps_data->als_correct_factor = STK_IRC_ALS_CORREC;
                }
#ifdef STK_DEBUG_PRINTF
                printk(KERN_INFO "%s: als=%d, ir=%d, als_correct_factor=%d", __func__, 
                                                reading, ps_data->ir_code, ps_data->als_correct_factor);
#endif
                ps_data->ir_code = 0;
        }
        reading = reading * ps_data->als_correct_factor / 1000;

        reading_lux = stk_alscode2lux(ps_data, reading);
        if(abs(ps_data->als_lux_last - reading_lux) >= STK_ALS_CHANGE_THD)
        {
                ps_data->als_lux_last = reading_lux;
				/*
                input_report_abs(ps_data->als_input_dev, ABS_MISC, reading_lux);
                input_sync(ps_data->als_input_dev);
                			*/
                input_report_abs(ps_data->ps_input_dev, ABS_MISC, reading_lux);
                input_sync(ps_data->ps_input_dev);                			
#ifdef STK_DEBUG_PRINTF
                printk(KERN_INFO "%s: als input event %d lux\n",__func__, reading_lux);
#endif
        }
        return;
}
#endif /* #ifdef STK_POLL_ALS */

#ifdef PS_VALUE_LOG
static enum hrtimer_restart stk_ps_log_timer_func(struct hrtimer *timer)
{
        struct stk3x1x_data *ps_data = container_of(timer, struct stk3x1x_data, ps_log_timer);
        queue_work(ps_data->stk_ps_log_wq, &ps_data->stk_ps_log_work);
        hrtimer_forward_now(&ps_data->ps_log_timer, ps_data->ps_log_poll_delay);
        return HRTIMER_RESTART;
}
static void stk_ps_log_poll_work_func(struct work_struct *work)
{
	struct stk3x1x_data *ps_data = container_of(work, struct stk3x1x_data, stk_ps_log_work);
	uint32_t reading;
	int32_t near_far_state;
    uint8_t org_flag_reg;

	if(ps_data->ps_enabled)
	{
		org_flag_reg = stk3x1x_get_flag(ps_data);
		if(org_flag_reg < 0)
			goto err_i2c_rw;

		if(!(org_flag_reg&STK_FLG_PSDR_MASK))
		{
			return;
		}

		near_far_state = (org_flag_reg & STK_FLG_NF_MASK)?1:0;
		reading = stk3x1x_get_ps_reading(ps_data);
		printk(KERN_INFO "%s: %d,%d,%d,%d\n",__func__, near_far_state, reading, ps_data->ps_thd_h, ps_data->ps_thd_l);
		return;
	}

err_i2c_rw:
	msleep(30);
	return;
}
#endif

#ifdef STK_POLL_PS
static enum hrtimer_restart stk_ps_timer_func(struct hrtimer *timer)
{
        struct stk3x1x_data *ps_data = container_of(timer, struct stk3x1x_data, ps_timer);
        queue_work(ps_data->stk_ps_wq, &ps_data->stk_ps_work);
        hrtimer_forward_now(&ps_data->ps_timer, ps_data->ps_poll_delay);
        return HRTIMER_RESTART;
}

static void stk_ps_poll_work_func(struct work_struct *work)
{
        struct stk3x1x_data *ps_data = container_of(work, struct stk3x1x_data, stk_ps_work);
        uint32_t reading;
        int32_t near_far_state;
    uint8_t org_flag_reg;
#ifdef STK_GES
        int32_t ret;
    //uint8_t disable_flag = 0;
        uint8_t disable_flag2 = 0, org_flag2_reg;
        if(ps_data->ges_enabled == 2)
        {
                ret = stk3x1x_get_flag2(ps_data);
                if(ret < 0)
                        goto err_i2c_rw;
                org_flag2_reg = ret;

                disable_flag2 = org_flag2_reg & (STK_FLG2_INT_GS_MASK | 
                        STK_FLG2_GS10_MASK | STK_FLG2_GS01_MASK);
                if(org_flag2_reg & STK_FLG2_GS10_MASK)
                {
                        printk(KERN_INFO "%s: >>>>>>>>>>>>\n", __func__);
                }
                if(org_flag2_reg & STK_FLG2_GS01_MASK)
                {
                        printk(KERN_INFO "%s: <<<<<<<<<<<<\n", __func__);
                }
                atomic_set(&ps_data->gesture2, (disable_flag2 & 
                                (STK_FLG2_GS10_MASK | STK_FLG2_GS01_MASK)));  

                if(disable_flag2)
                {
                        ret = stk3x1x_set_flag2(ps_data, org_flag2_reg, disable_flag2);
                        if(ret < 0)
                                goto err_i2c_rw;
                }
        }
#endif

        if(ps_data->ps_enabled)
        {
#ifdef STK_TUNE0
                // if(!(ps_data->psi_set))
                        // return;
#endif
                org_flag_reg = stk3x1x_get_flag(ps_data);
                if(org_flag_reg < 0)
                        goto err_i2c_rw;

                if(!(org_flag_reg&STK_FLG_PSDR_MASK))
                {
                        //printk(KERN_INFO "%s: ps is not ready\n", __func__);
                        return;
                }

                near_far_state = (org_flag_reg & STK_FLG_NF_MASK)?1:0;
                reading = stk3x1x_get_ps_reading(ps_data);
                if(ps_data->ps_distance_last != near_far_state)
                {
                        ps_data->ps_distance_last = near_far_state;
                        input_report_abs(ps_data->ps_input_dev, ABS_DISTANCE, near_far_state);
                        input_sync(ps_data->ps_input_dev);
                       // wake_lock_timeout(&ps_data->ps_wakelock, 3*HZ);
#ifdef STK_DEBUG_PRINTF
                        printk(KERN_INFO "%s: ps input event %d cm, ps code = %d\n",__func__, near_far_state, reading);
#endif
                }
                // ret = stk3x1x_set_flag(ps_data, org_flag_reg, disable_flag);
                // if(ret < 0)
                        // goto err_i2c_rw;
                return;
        }

err_i2c_rw:
        msleep(30);
        return;
}
#endif

#if (!defined(STK_POLL_PS) || !defined(STK_POLL_ALS))
static void stk_work_func(struct work_struct *work)
{
        uint32_t reading;
#if ((STK_INT_PS_MODE != 0x03) && (STK_INT_PS_MODE != 0x02))
    int32_t ret;
    uint8_t disable_flag = 0;
    uint8_t org_flag_reg;
#endif  /* #if ((STK_INT_PS_MODE != 0x03) && (STK_INT_PS_MODE != 0x02)) */

#ifndef CONFIG_STK_PS_ALS_USE_CHANGE_THRESHOLD
        uint32_t nLuxIndex;
#endif
        struct stk3x1x_data *ps_data = container_of(work, struct stk3x1x_data, stk_work);
        int32_t near_far_state;
        int32_t als_comperator;
#ifdef STK_GES
        uint8_t disable_flag2 = 0, org_flag2_reg;
#endif

#if (STK_INT_PS_MODE    == 0x03)
        near_far_state = gpio_get_value(ps_data->int_pin);
#elif   (STK_INT_PS_MODE        == 0x02)
        near_far_state = !(gpio_get_value(ps_data->int_pin));
#endif

#if ((STK_INT_PS_MODE == 0x03) || (STK_INT_PS_MODE      == 0x02))
        ps_data->ps_distance_last = near_far_state;
        input_report_abs(ps_data->ps_input_dev, ABS_DISTANCE, near_far_state);
        input_sync(ps_data->ps_input_dev);
       // wake_lock_timeout(&ps_data->ps_wakelock, 3*HZ);
        reading = stk3x1x_get_ps_reading(ps_data);
#ifdef STK_DEBUG_PRINTF
        printk(KERN_INFO "%s: ps input event %d cm, ps code = %d\n",__func__, near_far_state, reading);
#endif
#else
        /* mode 0x01 or 0x04 */
        org_flag_reg = stk3x1x_get_flag(ps_data);
        if(org_flag_reg < 0)
                goto err_i2c_rw;

    if (org_flag_reg & STK_FLG_ALSINT_MASK)
    {
                disable_flag |= STK_FLG_ALSINT_MASK;
        reading = stk3x1x_get_als_reading(ps_data);
                if(reading < 0)
                {
                        printk(KERN_ERR "%s: stk3x1x_get_als_reading fail, ret=%d", __func__, reading);
                        goto err_i2c_rw;
                }
#ifndef CONFIG_STK_PS_ALS_USE_CHANGE_THRESHOLD
        nLuxIndex = stk_get_lux_interval_index(reading);
        stk3x1x_set_als_thd_h(ps_data, code_threshold_table[nLuxIndex]);
        stk3x1x_set_als_thd_l(ps_data, code_threshold_table[nLuxIndex-1]);
#else
        stk_als_set_new_thd(ps_data, reading);
#endif //CONFIG_STK_PS_ALS_USE_CHANGE_THRESHOLD

                if(ps_data->ir_code)
                {
                        if(reading < STK_IRC_MAX_ALS_CODE && reading > STK_IRC_MIN_ALS_CODE && 
                        ps_data->ir_code > STK_IRC_MIN_IR_CODE)
                        {
                                als_comperator = reading * STK_IRC_ALS_NUMERA / STK_IRC_ALS_DENOMI;
                                if(ps_data->ir_code > als_comperator)
                                        ps_data->als_correct_factor = STK_IRC_ALS_CORREC;
                                else
                                        ps_data->als_correct_factor = 1000;
                        }
                        printk(KERN_INFO "%s: als=%d, ir=%d, als_correct_factor=%d", __func__, reading, ps_data->ir_code, ps_data->als_correct_factor);
                        ps_data->ir_code = 0;
                }

                reading = reading * ps_data->als_correct_factor / 1000;

                ps_data->als_lux_last = stk_alscode2lux(ps_data, reading);
                input_report_abs(ps_data->als_input_dev, ABS_MISC, ps_data->als_lux_last);
                input_sync(ps_data->als_input_dev);
#ifdef STK_DEBUG_PRINTF
                printk(KERN_INFO "%s: als input event %d lux\n",__func__, ps_data->als_lux_last);
#endif
    }
    if (org_flag_reg & STK_FLG_PSINT_MASK)
    {
                disable_flag |= STK_FLG_PSINT_MASK;
                near_far_state = (org_flag_reg & STK_FLG_NF_MASK)?1:0;

                ps_data->ps_distance_last = near_far_state;
                input_report_abs(ps_data->ps_input_dev, ABS_DISTANCE, near_far_state);
                input_sync(ps_data->ps_input_dev);
                //wake_lock_timeout(&ps_data->ps_wakelock, 3*HZ);
                reading = stk3x1x_get_ps_reading(ps_data);
#ifdef STK_DEBUG_PRINTF
                printk(KERN_INFO "%s: ps input event=%d, ps code = %d\n",__func__, near_far_state, reading);
#endif
        }

        if(disable_flag)
        {
                ret = stk3x1x_set_flag(ps_data, org_flag_reg, disable_flag);
                if(ret < 0)
                        goto err_i2c_rw;
        }
#endif
        usleep_range(1000, 2000);
        //msleep(1);
    enable_irq(ps_data->irq);
        return;

err_i2c_rw:
        msleep(30);
        enable_irq(ps_data->irq);
        return;
}

static irqreturn_t stk_oss_irq_handler(int irq, void *data)
{
        struct stk3x1x_data *pData = data;
        disable_irq_nosync(irq);
        queue_work(pData->stk_wq,&pData->stk_work);
        return IRQ_HANDLED;
}
#endif  /*      #if (!defined(STK_POLL_PS) || !defined(STK_POLL_ALS))   */
static int32_t stk3x1x_init_all_setting(struct i2c_client *client, struct stk3x1x_platform_data *plat_data)
{
        int32_t ret;
        struct stk3x1x_data *ps_data = i2c_get_clientdata(client);

        stk3x1x_proc_plat_data(ps_data, plat_data);
        ret = stk3x1x_software_reset(ps_data); 
        if(ret < 0)
                return ret;

        ret = stk3x1x_check_pid(ps_data);
        if(ret < 0)
                return ret;
        ret = stk3x1x_init_all_reg(ps_data);
        if(ret < 0)
                return ret;

        ps_data->als_enabled = false;
        ps_data->ps_enabled = false;
        ps_data->re_enable_als = false;
        ps_data->re_enable_ps = false;
        ps_data->ir_code = 0;
        ps_data->als_correct_factor = 1000;
        ps_data->first_boot = true;
#ifdef CONFIG_CALIBRATION
		ps_data->calibration_threshold_readed = false;
		ps_data->calibration_result = false;
#endif
#ifndef CONFIG_STK_PS_ALS_USE_CHANGE_THRESHOLD
        stk_init_code_threshold_table(ps_data);
#endif
#ifdef STK_TUNE0
        #ifdef QUALCOMM_PLATFORM
        ps_data->tune_zero_init_proc = false;
        ps_data->psi_set = 0;
        #else
        stk_ps_tune_zero_init(ps_data);
        #endif
#endif
#ifdef STK_ALS_FIR
        memset(&ps_data->fir, 0x00, sizeof(ps_data->fir));  
        atomic_set(&ps_data->firlength, STK_FIR_LEN);
#endif
        atomic_set(&ps_data->recv_reg, 0);  
#ifdef STK_GES
        ps_data->re_enable_ges = 0;
        atomic_set(&ps_data->gesture2, 0);  
        //memset(stk_ges_op, 0, sizeof(stk_ges_op));
#endif
#ifdef STK_IRS
        ps_data->als_data_index = 0;
#endif
        ps_data->ps_distance_last = 1;
    return 0;
}

#if (!defined(STK_POLL_PS) || !defined(STK_POLL_ALS))
static int stk3x1x_setup_irq(struct i2c_client *client)
{
        int irq, err = -EIO;
        struct stk3x1x_data *ps_data = i2c_get_clientdata(client);

#ifdef SPREADTRUM_PLATFORM
       // irq = sprd_alloc_gpio_irq(ps_data->int_pin);
       irq = gpio_to_irq(ps_data->int_pin);
#else
        irq = gpio_to_irq(ps_data->int_pin);
#endif
#ifdef STK_DEBUG_PRINTF
        printk(KERN_INFO "%s: int pin #=%d, irq=%d\n",__func__, ps_data->int_pin, irq);
#endif
        if (irq <= 0)
        {
                printk(KERN_ERR "irq number is not specified, irq # = %d, int pin=%d\n",irq, ps_data->int_pin);
                return irq;
        }
        ps_data->irq = irq;
        err = gpio_request(ps_data->int_pin,"stk-int");        
        if(err < 0)
        {
                printk(KERN_ERR "%s: gpio_request, err=%d", __func__, err);
                return err;
        }
        err = gpio_direction_input(ps_data->int_pin);
        if(err < 0)
        {
                printk(KERN_ERR "%s: gpio_direction_input, err=%d", __func__, err);
                return err;
        }
#if ((STK_INT_PS_MODE == 0x03) || (STK_INT_PS_MODE      == 0x02))
        err = request_any_context_irq(irq, stk_oss_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, DEVICE_NAME, ps_data);
#else
        err = request_any_context_irq(irq, stk_oss_irq_handler, IRQF_TRIGGER_LOW, DEVICE_NAME, ps_data);
#endif
        if (err < 0) 
        {
                printk(KERN_WARNING "%s: request_any_context_irq(%d) failed for (%d)\n", __func__, irq, err);
                goto err_request_any_context_irq;
        }
        disable_irq(irq);

        return 0;
err_request_any_context_irq:
#ifdef SPREADTRUM_PLATFORM
        //sprd_free_gpio_irq(ps_data->int_pin);
        gpio_free(ps_data->int_pin);
#else
        gpio_free(ps_data->int_pin);
#endif
        return err;
}
#endif


static int stk3x1x_suspend(struct device *dev)
{
        struct stk3x1x_data *ps_data = dev_get_drvdata(dev);
        int err;
#ifndef STK_POLL_PS
    struct i2c_client *client = to_i2c_client(dev);
#endif

        printk(KERN_INFO "%s", __func__);
#ifndef SPREADTRUM_PLATFORM
        mutex_lock(&ps_data->io_lock);  
#endif
#ifdef STK_CHK_REG
        err = stk3x1x_validate_n_handle(ps_data->client);
        if(err < 0)
        {
                printk(KERN_ERR "stk3x1x_validate_n_handle fail: %d\n", err); 
        }
        else if (err == 0xFF)
        {
                if(ps_data->ps_enabled)
                        stk3x1x_enable_ps(ps_data, 1, 0);
        }
#endif /* #ifdef STK_CHK_REG */
#ifdef STK_GES
        if(ps_data->ges_enabled == 1)
        {
                ps_data->re_enable_ges = ps_data->ges_enabled;
                stk3x1x_enable_ges(ps_data, 0, 1);
        }
        else if(ps_data->ges_enabled == 2)
        {
                ps_data->re_enable_ges = ps_data->ges_enabled;
                stk3x1x_enable_ges(ps_data, 0, 2);
        }
#endif
#ifndef SPREADTRUM_PLATFORM
        if(ps_data->als_enabled)
        {
                printk(KERN_INFO "%s: Enable ALS : 0\n", __func__);
                stk3x1x_enable_als(ps_data, 0);
                ps_data->re_enable_als = true;
        }  
#endif
        if(ps_data->ps_enabled)
        {
#ifdef STK_POLL_PS
                wake_lock(&ps_data->ps_nosuspend_wl);
#else
                if(device_may_wakeup(&client->dev))
                {
                        err = enable_irq_wake(ps_data->irq);
                        if (err)
                                printk(KERN_WARNING "%s: set_irq_wake(%d) failed, err=(%d)\n", __func__, ps_data->irq, err);
                }
                else
                {
                        printk(KERN_ERR "%s: not support wakeup source", __func__);
                }
#endif
        }
#ifndef SPREADTRUM_PLATFORM
        mutex_unlock(&ps_data->io_lock);
#endif
        return 0;
}

static int stk3x1x_resume(struct device *dev)
{
        struct stk3x1x_data *ps_data = dev_get_drvdata(dev);
        int err;
#ifndef STK_POLL_PS         
    struct i2c_client *client = to_i2c_client(dev);
#endif
        printk("%s called\n", __func__);
        printk(KERN_INFO "%s", __func__);
#ifndef SPREADTRUM_PLATFORM
        mutex_lock(&ps_data->io_lock); 
#endif
#ifdef STK_CHK_REG
        err = stk3x1x_validate_n_handle(ps_data->client);
        if(err < 0)
        {
                printk(KERN_ERR "stk3x1x_validate_n_handle fail: %d\n", err); 
        }
        else if (err == 0xFF)
        {
                if(ps_data->ps_enabled)
                        stk3x1x_enable_ps(ps_data, 1, 0);
        }
#endif /* #ifdef STK_CHK_REG */
#ifdef STK_GES
        if(ps_data->re_enable_ges == 1)
        {
                stk3x1x_enable_ges(ps_data, 1, 1);
                ps_data->re_enable_ges = 0;
        }
        else if(ps_data->re_enable_ges == 2)
        {
                stk3x1x_enable_ges(ps_data, 1, 2);
                ps_data->re_enable_ges = 0;
        }
#endif
#ifndef SPREADTRUM_PLATFORM
        if(ps_data->re_enable_als)
        {
                printk(KERN_INFO "%s: Enable ALS : 1\n", __func__);
                stk3x1x_enable_als(ps_data, 1);
                ps_data->re_enable_als = false;
        }
#endif
        if(ps_data->ps_enabled)
        {
#ifdef STK_POLL_PS
                wake_unlock(&ps_data->ps_nosuspend_wl);
#else
                if(device_may_wakeup(&client->dev))
                {
                        err = disable_irq_wake(ps_data->irq);
                        if (err)
                                printk(KERN_WARNING "%s: disable_irq_wake(%d) failed, err=(%d)\n", __func__, ps_data->irq, err);
                }
#endif
        }
#ifndef SPREADTRUM_PLATFORM
        mutex_unlock(&ps_data->io_lock);
#endif
        return 0;
}

static const struct dev_pm_ops stk3x1x_pm_ops = {
        SET_SYSTEM_SLEEP_PM_OPS(stk3x1x_suspend, stk3x1x_resume)
};

//#ifdef CONFIG_HAS_EARLYSUSPEND
#if 0
static void stk3x1x_early_suspend(struct early_suspend *h)
{

}

static void stk3x1x_late_resume(struct early_suspend *h)
{

}
#endif  //#ifdef CONFIG_HAS_EARLYSUSPEND


#ifdef STK_QUALCOMM_POWER_CTRL
static int stk3x1x_power_ctl(struct stk3x1x_data *data, bool on)
{
        int ret = 0;

        if (!on && data->power_enabled) {
                ret = regulator_disable(data->vdd);
                if (ret) {
                        dev_err(&data->client->dev,
                                "Regulator vdd disable failed ret=%d\n", ret);
                        return ret;
                }

                ret = regulator_disable(data->vio);
                if (ret) {
                        dev_err(&data->client->dev,
                                "Regulator vio disable failed ret=%d\n", ret);
                        regulator_enable(data->vdd);
                        return ret;
                }
                data->power_enabled = on;
                printk(KERN_INFO "%s: disable stk3x1x power", __func__);
                dev_dbg(&data->client->dev, "stk3x1x_power_ctl on=%d\n",
                                on);
        } else if (on && !data->power_enabled) {
                ret = regulator_enable(data->vdd);
                if (ret) {
                        dev_err(&data->client->dev,
                                "Regulator vdd enable failed ret=%d\n", ret);
                        return ret;
                }

                ret = regulator_enable(data->vio);
                if (ret) {
                        dev_err(&data->client->dev,
                                "Regulator vio enable failed ret=%d\n", ret);
                        regulator_disable(data->vdd);
                        return ret;
                }
                data->power_enabled = on;
                printk(KERN_INFO "%s: enable stk3x1x power", __func__);
                dev_dbg(&data->client->dev, "stk3x1x_power_ctl on=%d\n",
                                on);
        } else {
                dev_warn(&data->client->dev,
                                "Power on=%d. enabled=%d\n",
                                on, data->power_enabled);
        }

        return ret;
}

static int stk3x1x_power_init(struct stk3x1x_data *data, bool on)
{
        int ret;

        if (!on) {
                if (regulator_count_voltages(data->vdd) > 0)
                        regulator_set_voltage(data->vdd,
                                        0, STK3X1X_VDD_MAX_UV);

                regulator_put(data->vdd);

                if (regulator_count_voltages(data->vio) > 0)
                        regulator_set_voltage(data->vio,
                                        0, STK3X1X_VIO_MAX_UV);

                regulator_put(data->vio);
        } else {
                data->vdd = regulator_get(&data->client->dev, "vdd");
                if (IS_ERR(data->vdd)) {
                        ret = PTR_ERR(data->vdd);
                        dev_err(&data->client->dev,
                                "Regulator get failed vdd ret=%d\n", ret);
                        return ret;
                }

                if (regulator_count_voltages(data->vdd) > 0) {
                        ret = regulator_set_voltage(data->vdd,
                                        STK3X1X_VDD_MIN_UV,
                                        STK3X1X_VDD_MAX_UV);
                        if (ret) {
                                dev_err(&data->client->dev,
                                        "Regulator set failed vdd ret=%d\n",
                                        ret);
                                goto reg_vdd_put;
                        }
                }

                data->vio = regulator_get(&data->client->dev, "vio");
                if (IS_ERR(data->vio)) {
                        ret = PTR_ERR(data->vio);
                        dev_err(&data->client->dev,
                                "Regulator get failed vio ret=%d\n", ret);
                        goto reg_vdd_set;
                }

                if (regulator_count_voltages(data->vio) > 0) {
                        ret = regulator_set_voltage(data->vio,
                                        STK3X1X_VIO_MIN_UV,
                                        STK3X1X_VIO_MAX_UV);
                        if (ret) {
                                dev_err(&data->client->dev,
                                "Regulator set failed vio ret=%d\n", ret);
                                goto reg_vio_put;
                        }
                }
        }

        return 0;

reg_vio_put:
        regulator_put(data->vio);
reg_vdd_set:
        if (regulator_count_voltages(data->vdd) > 0)
                regulator_set_voltage(data->vdd, 0, STK3X1X_VDD_MAX_UV);
reg_vdd_put:
        regulator_put(data->vdd);
        return ret;
}

static int stk3x1x_device_ctl(struct stk3x1x_data *ps_data, bool enable)
{
        int ret;
        struct device *dev = &ps_data->client->dev;

        if (enable && !ps_data->power_enabled) {
                ret = stk3x1x_power_ctl(ps_data, true);
                if (ret) {
                        dev_err(dev, "Failed to enable device power\n");
                        goto err_exit;
                }
                ret = stk3x1x_init_all_setting(ps_data->client, ps_data->pdata);
                if (ret < 0) {
                        stk3x1x_power_ctl(ps_data, false);
                        dev_err(dev, "Failed to re-init device setting\n");
                        goto err_exit;
                }
        } else if (!enable && ps_data->power_enabled) {
        #ifdef STK_GES
                if (!ps_data->als_enabled && !ps_data->ps_enabled && !ps_data->ges_enabled) {
        #else
                if (!ps_data->als_enabled && !ps_data->ps_enabled) {
        #endif
                        ret = stk3x1x_power_ctl(ps_data, false);
                        if (ret) {
                                dev_err(dev, "Failed to disable device power\n");
                                goto err_exit;
                        }
                } else {
                        dev_dbg(dev, "device control: als_enabled=%d, ps_enabled=%d\n",
                                ps_data->als_enabled, ps_data->ps_enabled);
                }
        } else {
                dev_dbg(dev, "device control: enable=%d, power_enabled=%d\n",
                        enable, ps_data->power_enabled);
        }
        return 0;

err_exit:
        return ret;
}
#endif  /* #ifdef STK_QUALCOMM_POWER_CTRL */

#ifdef CONFIG_OF
static int stk3x1x_parse_dt(struct device *dev,
                        struct stk3x1x_platform_data *pdata)
{
#if 1
    //    int rc;
        struct device_node *np = dev->of_node;
        //u32 temp_val;

	printk("%s,pdata->int_pin = %d\n",__func__,pdata->int_pin);
	
        pdata->int_pin = of_get_named_gpio(np, "stk,irq-gpio",
                                0);
        if (pdata->int_pin < 0) {
                dev_err(dev, "Unable to read irq-gpio\n");
                return pdata->int_pin;
        }
	printk("%s,pdata->int_pin = %d\n",__func__,pdata->int_pin);
/*
        rc = of_property_read_u32(np, "stk,transmittance", &temp_val);
        if (!rc)
                pdata->transmittance = temp_val;
        else {
                dev_err(dev, "Unable to read transmittance\n");
                return rc;
        }

        rc = of_property_read_u32(np, "stk,state-reg", &temp_val);
        if (!rc)
                pdata->state_reg = temp_val;
        else {
                dev_err(dev, "Unable to read state-reg\n");
                return rc;
        }

        rc = of_property_read_u32(np, "stk,psctrl-reg", &temp_val);
        if (!rc)
                pdata->psctrl_reg = (u8)temp_val;
        else {
                dev_err(dev, "Unable to read psctrl-reg\n");
                return rc;
        }

        rc = of_property_read_u32(np, "stk,alsctrl-reg", &temp_val);
        if (!rc)
                pdata->alsctrl_reg = (u8)temp_val;
        else {
                dev_err(dev, "Unable to read alsctrl-reg\n");
                return rc;
        }

        rc = of_property_read_u32(np, "stk,ledctrl-reg", &temp_val);
        if (!rc)
                pdata->ledctrl_reg = (u8)temp_val;
        else {
                dev_err(dev, "Unable to read ledctrl-reg\n");
                return rc;
        }

        rc = of_property_read_u32(np, "stk,wait-reg", &temp_val);
        if (!rc)
                pdata->wait_reg = (u8)temp_val;
        else {
                dev_err(dev, "Unable to read wait-reg\n");
                return rc;
        }

        rc = of_property_read_u32(np, "stk,ps-thdh", &temp_val);
        if (!rc)
                pdata->ps_thd_h = (u16)temp_val;
        else {
                dev_err(dev, "Unable to read ps-thdh\n");
                return rc;
        }

        rc = of_property_read_u32(np, "stk,ps-thdl", &temp_val);
        if (!rc)
                pdata->ps_thd_l = (u16)temp_val;
        else {
                dev_err(dev, "Unable to read ps-thdl\n");
                return rc;
        }*/

        //pdata->use_fir = of_property_read_bool(np, "stk,use-fir");
#endif

        return 0;
}
#else
static int stk3x1x_parse_dt(struct device *dev,
                        struct stk3x1x_platform_data *pdata)
{
        return -ENODEV;
}
#endif /* !CONFIG_OF */

static int stk3x1x_set_wq(struct stk3x1x_data *ps_data)
{
#ifdef STK_POLL_ALS
        ps_data->stk_als_wq = create_singlethread_workqueue("stk_als_wq");
        INIT_WORK(&ps_data->stk_als_work, stk_als_poll_work_func);
        hrtimer_init(&ps_data->als_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        ps_data->als_poll_delay = ns_to_ktime(110 * NSEC_PER_MSEC);
        ps_data->als_timer.function = stk_als_timer_func;
#endif

#ifdef STK_POLL_PS
        ps_data->stk_ps_wq = create_singlethread_workqueue("stk_ps_wq");
        INIT_WORK(&ps_data->stk_ps_work, stk_ps_poll_work_func);
        hrtimer_init(&ps_data->ps_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        ps_data->ps_poll_delay = ns_to_ktime(60 * NSEC_PER_MSEC);
        ps_data->ps_timer.function = stk_ps_timer_func;
#endif

#ifdef PS_VALUE_LOG
        ps_data->stk_ps_log_wq = create_singlethread_workqueue("stk_ps_log_wq");
        INIT_WORK(&ps_data->stk_ps_log_work, stk_ps_log_poll_work_func);
        hrtimer_init(&ps_data->ps_log_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        ps_data->ps_log_poll_delay = ns_to_ktime(60 * NSEC_PER_MSEC);
        ps_data->ps_log_timer.function = stk_ps_log_timer_func;
#endif

#ifdef STK_TUNE0
        ps_data->stk_ps_tune0_wq = create_singlethread_workqueue("stk_ps_tune0_wq");
        INIT_WORK(&ps_data->stk_ps_tune0_work, stk_ps_tune0_work_func);
        hrtimer_init(&ps_data->ps_tune0_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        ps_data->ps_tune0_delay = ns_to_ktime(60 * NSEC_PER_MSEC);
        ps_data->ps_tune0_timer.function = stk_ps_tune0_timer_func;
#endif

#if (!defined(STK_POLL_ALS) || !defined(STK_POLL_PS))
        ps_data->stk_wq = create_singlethread_workqueue("stk_wq");
        INIT_WORK(&ps_data->stk_work, stk_work_func);
#endif
        return 0;
}
/*
static int stk3x1x_pls_create_sysfs(struct i2c_client *client)
{
	int err;
	struct device *dev = &(client->dev);
	printk(KERN_INFO "%s\n", __func__);

//	err = device_create_file(dev, &stk_allreg);

	return err;
}
*/

#ifdef CONFIG_CALIBRATION
static char step;
static uint32_t ps_adc_open_sky = 0;
static uint32_t ps_adc_near = 0;
static uint32_t ps_adc_far = 0;

#define FILE_NAME_LENGTH 64
#define FILE_LENGTH 32
#define PRODUCTINFO_FILEPATH "/mnt/vendor/productinfo/"
#define PSENSOR_CALIBRATION_FILENAME "psensor_calibration.ini"

#define NEAR_RANGE_MIN 35
#define FAR_RANGE_MIN 20

static int calibration_threshold_read(struct stk3x1x_data *ps_data)
{
    char file_path[FILE_NAME_LENGTH] = { 0 };
	char *filp_buf;
    struct file *filp = NULL;
    loff_t pos = 0;
	int ret, file_len;
	uint32_t ps_adc1, ps_adc2, ps_adc3, thdl, thdh;

    snprintf(file_path, FILE_NAME_LENGTH, "%s%s", PRODUCTINFO_FILEPATH, PSENSOR_CALIBRATION_FILENAME);
	filp = filp_open(file_path, O_RDONLY, (S_IRUSR|S_IRGRP));
    if (IS_ERR(filp)) {
        pr_err("Error:(%s) open %s file fail\n", __func__, file_path);
        return -ENOENT;
    }

	file_len = filp->f_inode->i_size;
	filp_buf = kmalloc(file_len,GFP_KERNEL);
	if (!filp_buf) {
		filp_close(filp, NULL);
		ret = -ENOMEM;
		return ret;
	}

	ret = kernel_read(filp, filp_buf, file_len, &pos);
    if (ret < 0){
		filp_close(filp, NULL);
        pr_err("read file fail\n");
		goto err;
	}

    filp_close(filp, NULL);

	ret = sscanf(filp_buf,"%d,%d,%d,thdl=%d,thdh=%d", &ps_adc1, &ps_adc2, &ps_adc3, &thdl, &thdh);
	if(!ret){
		ret = sscanf(filp_buf,"thdl=%d,thdh=%d", &thdl, &thdh);
		if(!ret){
			pr_err("Error:(%s) calibration file data format error\n", __func__);
			goto err;
		}
	}

	ps_data->ps_high_thd_boot = ps_adc1 + thdh;
	ps_data->ps_low_thd_boot = ps_adc1 + thdl;

	if(thdl < NEAR_RANGE_MIN || thdh < FAR_RANGE_MIN){
		pr_err("Error:(%s) calibration data range error\n", __func__);
		return -EINVAL;
	}
	
	ps_data->stk_lt_n_ct = thdl;
	ps_data->stk_ht_n_ct = thdh;

	pr_info("(%s) %d, %d, %d, thdl = %u, thdh = %u\n", __func__, ps_adc1, ps_adc2, ps_adc3, thdl, thdh);
	ps_data->calibration_threshold_readed = true;

err:
	if(filp_buf)
		kfree(filp_buf);

	return ret;
}
static uint32_t calibration_cal_threshold(struct stk3x1x_data *ps_data)
{
    char file_path[FILE_NAME_LENGTH] = { 0 };
	char filp_buf[FILE_LENGTH] = { 0 };
    struct file *filp = NULL;
    loff_t pos = 0;
	int ret, file_len;

	if(!ps_adc_open_sky || !ps_adc_near || !ps_adc_far){
		pr_err("Error: (%s) can't calculate threshold value due to incomplete calibration step!\n", __func__);
		return -EINVAL;
	}

	if(ps_adc_open_sky + FAR_RANGE_MIN < ps_adc_far && ps_adc_open_sky + NEAR_RANGE_MIN < ps_adc_near){
		ps_data->stk_lt_n_ct = (ps_adc_near - ps_adc_open_sky) & 0xFFFF;
		ps_data->stk_ht_n_ct = (ps_adc_far - ps_adc_open_sky) & 0xFFFF;

		stk3x1x_set_ps_thd_l(ps_data, ps_adc_near);
		stk3x1x_set_ps_thd_h(ps_data, ps_adc_far);

		snprintf(file_path, FILE_NAME_LENGTH, "%s%s", PRODUCTINFO_FILEPATH, PSENSOR_CALIBRATION_FILENAME);
		filp = filp_open(file_path, O_CREAT | O_RDWR, 0666);
		if (IS_ERR(filp)) {
			pr_err("(%s) open %s file fail\n", __func__, file_path);
			return -ENOENT;
		}

		file_len = snprintf(filp_buf, FILE_LENGTH, "%d,%d,%d,thdl=%u,thdh=%u\n", ps_adc_open_sky, ps_adc_near, ps_adc_far, ps_data->stk_lt_n_ct, ps_data->stk_ht_n_ct);

		ret = kernel_write(filp, filp_buf, file_len, &pos);
		if (ret < 0)
			pr_err("write file fail\n");

		filp_close(filp, NULL);
		ps_data->calibration_threshold_readed = false;
		ps_data->calibration_result = true;
	}else{
		pr_err("Error: (%s) calibration ps value illegal!\n", __func__);
		return -EINVAL;
	}

	return ret;
}

static uint32_t calibration_get_ps_data(struct stk3x1x_data *ps_data)
{
	uint32_t ps_adc = 0;
	uint32_t ps_adc_sum = 0;
	uint32_t ret = 0;
	int count = 0;

	mdelay(1000);

	while(count < 5){
		ret = stk_ps_val(ps_data);
		if(ret == 0xFFFF)
		{
				ps_data->data_count = -1;
				stk_ps_tune_zero_final(ps_data);
				return 0;
		}

		ps_adc = stk3x1x_get_ps_reading(ps_data);
		printk(KERN_INFO "%s: ps_adc #%d=%d\n", __func__, count, ps_adc);
		if(ps_adc < 0)
				return ps_adc;

		ps_adc_sum +=  ps_adc;
		count++;
		mdelay(400);
	}

	ret = ps_adc_sum / count;
	return ret;
}

static ssize_t sprocomm_read_psensor_calibration(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct stk3x1x_data *ps_data;
	char result[10];
	int len;

	pr_info("%s", __func__);

	if(!stk3x1x_i2c_client){
		pr_err("Error: (%s) stk3x1x_i2c_client is NULL!\n", __func__);
		return -EFAULT;
	}

	ps_data = i2c_get_clientdata(stk3x1x_i2c_client);

	len = snprintf(result, sizeof(result), "%s\n", ps_data->calibration_result ? "pass" : "fail");

	if(copy_to_user(buffer, (char *)result, len))
		return -EFAULT;

	return len;
}

static ssize_t sprocomm_write_psensor_calibration(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct stk3x1x_data *ps_data;
	uint32_t ps_adc;
	char buf[10];

	pr_info("%s", __func__);

	if(!stk3x1x_i2c_client){
		pr_err("Error: (%s) stk3x1x_i2c_client is NULL!\n", __func__);
		return -EFAULT;
	}

	if(count > 10){
		pr_err("Error: (%s) size should not be more than 10 chars!\n", __func__);
		return -EINVAL;
	}

	if(copy_from_user(buf, buffer, count))
		return -EFAULT;

	ps_data = i2c_get_clientdata(stk3x1x_i2c_client);

	if(!ps_data->ps_enabled){
		pr_err("Error: (%s) Psensor not enabled!\n", __func__);
		return -EFAULT;
	}

	ps_adc = calibration_get_ps_data(ps_data);
	step = buf[0];
	pr_info("(%s) operation: %c ps_adc: %u\n", __func__, step, ps_adc);

	if(step == '0'){
		ps_data->calibration_result = false;
		ps_adc_open_sky = ps_adc;
	}else if(step == '1'){
		if(!ps_adc_open_sky)
			return -EINVAL;
		ps_adc_near = ps_adc;
	}else if(step == '2'){
		if(!ps_adc_open_sky || !ps_adc_near)
			return -EINVAL;
		ps_adc_far = ps_adc;
		calibration_cal_threshold(ps_data);
	}else
		pr_warn("Warning: (%s) unkonwn step %c\n", __func__, buf[0]);

	return count;
}

static struct file_operations psensor_proc_info_fops = {
	.read = sprocomm_read_psensor_calibration,
    .write = sprocomm_write_psensor_calibration,
};

static ssize_t sprocomm_psensor_calibration_value(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct stk3x1x_data *ps_data;
	char result[40];
	uint32_t ps_adc = 0, calibration_value = 0;
	int len, ret;

	pr_info("%s", __func__);

	if(!stk3x1x_i2c_client){
		pr_err("Error: (%s) stk3x1x_i2c_client is NULL!\n", __func__);
		return -EFAULT;
	}

	if(step == '0'){
		calibration_value = ps_adc_open_sky;
	}else if(step == '1'){
		calibration_value = ps_adc_near;
	}else if(step == '2'){
		calibration_value = ps_adc_far;
	}else{
		pr_warn("Warning: (%s) unkonwn step %c\n", __func__, step);
		return -EINVAL;
	}

	ps_data = i2c_get_clientdata(stk3x1x_i2c_client);

	ret = stk_ps_val(ps_data);
	if(ret == 0xFFFF)
	{
			ps_data->data_count = -1;
			stk_ps_tune_zero_final(ps_data);
			return 0;
	}

	ps_adc = stk3x1x_get_ps_reading(ps_data);
	len = snprintf(result, sizeof(result), "cali_value=%d,read_value=%d\n", calibration_value, ps_adc);

	if(copy_to_user(buffer, (char *)result, len))
		return -EFAULT;

	return len;
}

static struct file_operations psensor_proc_value_fops = {
	.read = sprocomm_psensor_calibration_value,
};

static int sprocomm_create_proc_psensor_calibration(void)
{
	struct proc_dir_entry *psensor_info_entry = NULL;
	struct proc_dir_entry *psensor_value_entry = NULL;

	psensor_info_entry = proc_create("psensor_calibration", 0666, NULL, &psensor_proc_info_fops);
	if (psensor_info_entry == NULL)
    {
        printk("Create psensor_calibration fail\n");
    }

	psensor_value_entry = proc_create("psensor_calibration_value", 0444, NULL, &psensor_proc_value_fops);
	if (psensor_info_entry == NULL)
    {
        printk("Create psensor_calibration_value fail\n");
    }
	return 0;
}
#endif

static int stk3x1x_set_input_devices(struct i2c_client *client, struct stk3x1x_data *ps_data)
{
        int err;
		printk(KERN_INFO "%s\n", __func__);

		err = misc_register(&stk3x1x_pls_device);
		if (err) {
			printk("%s: misc register failed\n", __func__);
			err = -ENOMEM;
            return err;
		}

        ps_data->als_input_dev = input_allocate_device();
        if (ps_data->als_input_dev==NULL)
        {
                printk(KERN_ERR "%s: could not allocate als device\n", __func__);
                err = -ENOMEM;
                return err;
        }
        ps_data->ps_input_dev = input_allocate_device();
        if (ps_data->ps_input_dev==NULL)
        {
                printk(KERN_ERR "%s: could not allocate ps device\n", __func__);
                err = -ENOMEM;
                return err;
        }
        ps_data->als_input_dev->name = ALS_NAME;
        ps_data->ps_input_dev->name = PS_NAME;
		ps_data->ps_input_dev->phys  = PS_NAME;
		ps_data->ps_input_dev->id.bustype = BUS_I2C;
		ps_data->ps_input_dev->dev.parent = &client->dev;
		ps_data->ps_input_dev->id.vendor = 0x0001;
		ps_data->ps_input_dev->id.product = 0x0001;
		ps_data->ps_input_dev->id.version = 0x0010;		
		
        set_bit(EV_ABS, ps_data->als_input_dev->evbit);
        set_bit(EV_ABS, ps_data->ps_input_dev->evbit);
        input_set_abs_params(ps_data->als_input_dev, ABS_MISC, 0, stk_alscode2lux(ps_data, (1<<16)-1), 0, 0);
        input_set_abs_params(ps_data->ps_input_dev, ABS_DISTANCE, 0,1, 0, 0);
		input_set_abs_params(ps_data->ps_input_dev, ABS_MISC, 0, 100001, 0, 0);
        err = input_register_device(ps_data->als_input_dev);
        if (err<0)
        {
                printk(KERN_ERR "%s: can not register als input device\n", __func__);
                return err;
        }
        err = input_register_device(ps_data->ps_input_dev);
        if (err<0)
        {
                printk(KERN_ERR "%s: can not register ps input device\n", __func__);
                return err;
        }

		
        err = sysfs_create_group(&ps_data->als_input_dev->dev.kobj, &stk_als_attribute_group);
        if (err < 0) 
        {
                printk(KERN_ERR "%s:could not create sysfs group for als\n", __func__);
                return err;
        }
        err = sysfs_create_group(&ps_data->ps_input_dev->dev.kobj, &stk_ps_attribute_group);
        if (err < 0) 
        {
                printk(KERN_ERR "%s:could not create sysfs group for ps\n", __func__);
                return err;
        }
        	
        //stk3x1x_pls_create_sysfs(client);
#ifdef CONFIG_CALIBRATION
		sprocomm_create_proc_psensor_calibration();
#endif
		
        input_set_drvdata(ps_data->als_input_dev, ps_data);
        input_set_drvdata(ps_data->ps_input_dev, ps_data);

#ifdef STK_GES
        ps_data->ges_input_dev = input_allocate_device();
        if (ps_data->ges_input_dev==NULL)
        {
                printk(KERN_ERR "%s: could not allocate ps device\n", __func__);
                err = -ENOMEM;
                return err;
        }
        ps_data->ges_input_dev->name = "stk_ges";
        ps_data->ges_input_dev->evbit[0] = BIT_MASK(EV_KEY);
        set_bit(KEY_PAGEUP, ps_data->ges_input_dev->keybit);
        set_bit(KEY_PAGEDOWN, ps_data->ges_input_dev->keybit);
        set_bit(KEY_VOLUMEUP, ps_data->ges_input_dev->keybit);
        set_bit(KEY_VOLUMEDOWN, ps_data->ges_input_dev->keybit);
        /*
        set_bit(KEY_LEFT, ps_data->ges_input_dev->keybit);
        set_bit(KEY_RIGHT, ps_data->ges_input_dev->keybit);
        set_bit(KEY_UP, ps_data->ges_input_dev->keybit);
        set_bit(KEY_DOWN, ps_data->ges_input_dev->keybit);
        */
        err = input_register_device(ps_data->ges_input_dev);
        if (err<0)
        {
                printk(KERN_ERR "%s: can not register ps input device\n", __func__);
                return err;
        }

        err = sysfs_create_group(&ps_data->ges_input_dev->dev.kobj, &stk_ges_attribute_group);
        if (err < 0) 
        {
                printk(KERN_ERR "%s:could not create sysfs group for ps\n", __func__);
                return err;
        }
        input_set_drvdata(ps_data->ges_input_dev, ps_data);
#endif

        return 0;
}

static int stk3x1x_probe(struct i2c_client *client,
                        const struct i2c_device_id *id)
{
    int err = -ENODEV;
    struct stk3x1x_data *ps_data;
        struct stk3x1x_platform_data *plat_data;
    printk(KERN_INFO "%s: driver version = %s\n", __func__, DRIVER_VERSION);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
    {
        printk(KERN_ERR "%s: No Support for I2C_FUNC_I2C\n", __func__);
        return -ENODEV;
    }

        ps_data = kzalloc(sizeof(struct stk3x1x_data),GFP_KERNEL);
        if(!ps_data)
        {
                printk(KERN_ERR "%s: failed to allocate stk3x1x_data\n", __func__);
                return -ENOMEM;
        }
        ps_data->client = client;
        i2c_set_clientdata(client,ps_data);
        mutex_init(&ps_data->io_lock);
       // wake_lock_init(&ps_data->ps_wakelock,WAKE_LOCK_SUSPEND, "stk_input_wakelock");

#ifdef STK_POLL_PS
       // wake_lock_init(&ps_data->ps_nosuspend_wl,WAKE_LOCK_SUSPEND, "stk_nosuspend_wakelock");
#endif

        if (client->dev.of_node) {
                printk(KERN_INFO "%s: probe with device tree\n", __func__);
                plat_data = devm_kzalloc(&client->dev,
                        sizeof(struct stk3x1x_platform_data), GFP_KERNEL);
                if (!plat_data) {
                        dev_err(&client->dev, "Failed to allocate memory\n");
                        return -ENOMEM;
                }

                err = stk3x1x_parse_dt(&client->dev, plat_data);
//huanggq
                  plat_data->state_reg = stk3x1x_pfdata.state_reg; 
                  plat_data->psctrl_reg = stk3x1x_pfdata.psctrl_reg;
                  plat_data->alsctrl_reg = stk3x1x_pfdata.alsctrl_reg; 
                  plat_data->ledctrl_reg = stk3x1x_pfdata.ledctrl_reg; 
                  plat_data->wait_reg = stk3x1x_pfdata.wait_reg;
                  plat_data->ps_thd_h = stk3x1x_pfdata.ps_thd_h; 
                  plat_data->ps_thd_l = stk3x1x_pfdata.ps_thd_l;
                  //plat_data->int_pin = stk3x1x_pfdata.int_pin;
                  plat_data->transmittance = stk3x1x_pfdata.transmittance;
//end
                dev_err(&client->dev,
                        "%s: stk3x1x_parse_dt ret=%d\n", __func__, err);
                if (err)
                        return err;
        } else {
                printk(KERN_INFO "%s: probe with platform data\n", __func__);
#ifdef SPREADTRUM_PLATFORM
                plat_data = &stk3x1x_pfdata;
#else
                plat_data = client->dev.platform_data;
#endif
        }
        if (!plat_data) {
                dev_err(&client->dev,
                        "%s: no stk3x1x platform data!\n", __func__);
                goto err_als_input_allocate;
        }
        ps_data->als_transmittance = plat_data->transmittance;
        ps_data->int_pin = plat_data->int_pin;
        ps_data->pdata = plat_data;

        if (ps_data->als_transmittance == 0) {
                dev_err(&client->dev,
                        "%s: Please set als_transmittance\n", __func__);
                goto err_als_input_allocate;
        }

		stk3x1x_i2c_client = client;
		printk("%s: I2C addr = %d\n", __func__, client->addr);	
        stk3x1x_set_wq(ps_data);
#ifdef QUALCOMM_PLATFORM
        ps_data->ps_thd_h = 0;
        ps_data->ps_thd_l = 0;
#endif
        ps_data->stk_max_min_diff = STK_MAX_MIN_DIFF;
        ps_data->stk_lt_n_ct = STK_LT_N_CT;
        ps_data->stk_ht_n_ct = STK_HT_N_CT;

#ifdef STK_QUALCOMM_POWER_CTRL
        err = stk3x1x_power_init(ps_data, true);
        if (err)
                goto err_init_all_setting;

        err = stk3x1x_power_ctl(ps_data, true);
        if (err)
                goto err_power_on;

        ps_data->als_enabled = false;
        ps_data->ps_enabled = false;
#endif

        err = stk3x1x_init_all_setting(client, plat_data);
        if(err < 0)
                goto err_init_all_setting;

        err = stk3x1x_set_input_devices(client, ps_data);
        if(err < 0)
                goto err_setup_input_device;

#if (!defined(STK_POLL_ALS) || !defined(STK_POLL_PS))
        err = stk3x1x_setup_irq(client);
        if(err < 0)
                goto err_stk3x1x_setup_irq;
#endif
        device_init_wakeup(&client->dev, true);

//#ifdef CONFIG_HAS_EARLYSUSPEND
#if 0
        ps_data->stk_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
        ps_data->stk_early_suspend.suspend = stk3x1x_early_suspend;
        ps_data->stk_early_suspend.resume = stk3x1x_late_resume;
        register_early_suspend(&ps_data->stk_early_suspend);
#endif

#ifdef STK_QUALCOMM_POWER_CTRL
        /* enable device power only when it is enabled */
        err = stk3x1x_power_ctl(ps_data, false);
        if (err)
                goto err_stk3x1x_setup_irq;
#endif
        printk(KERN_INFO "%s: probe successfully\n", __func__);
        return 0;

        //device_init_wakeup(&client->dev, false);
#ifdef STK_QUALCOMM_POWER_CTRL
err_stk3x1x_setup_irq:
#endif
#if (!defined(STK_POLL_ALS) || !defined(STK_POLL_PS))
err_stk3x1x_setup_irq:
        free_irq(ps_data->irq, ps_data);
        #ifdef SPREADTRUM_PLATFORM
                //sprd_free_gpio_irq(ps_data->int_pin);
                 gpio_free(ps_data->int_pin);
        #else
                gpio_free(ps_data->int_pin);
        #endif
		
#endif
err_setup_input_device:
#ifdef STK_GES
        sysfs_remove_group(&ps_data->ges_input_dev->dev.kobj, &stk_ges_attribute_group);
        input_unregister_device(ps_data->ges_input_dev);
        input_free_device(ps_data->ges_input_dev);
#endif
        sysfs_remove_group(&ps_data->ps_input_dev->dev.kobj, &stk_ps_attribute_group);
        sysfs_remove_group(&ps_data->als_input_dev->dev.kobj, &stk_als_attribute_group);
        input_unregister_device(ps_data->ps_input_dev);
        input_unregister_device(ps_data->als_input_dev);
        input_free_device(ps_data->ps_input_dev);
        input_free_device(ps_data->als_input_dev);
#ifdef STK_QUALCOMM_POWER_CTRL
        stk3x1x_power_ctl(ps_data, false);
err_power_on:
        stk3x1x_power_init(ps_data, false);
#endif
err_init_all_setting:
#ifdef STK_POLL_ALS
        hrtimer_try_to_cancel(&ps_data->als_timer);
        destroy_workqueue(ps_data->stk_als_wq);
#endif
#ifdef STK_TUNE0
        destroy_workqueue(ps_data->stk_ps_tune0_wq);
#endif
#ifdef STK_POLL_PS
        hrtimer_try_to_cancel(&ps_data->ps_timer);
        destroy_workqueue(ps_data->stk_ps_wq);
#endif
#if (!defined(STK_POLL_ALS) || !defined(STK_POLL_PS))
        destroy_workqueue(ps_data->stk_wq);
#endif
		stk3x1x_i2c_client = NULL;  
err_als_input_allocate:
#ifdef STK_POLL_PS
    wake_lock_destroy(&ps_data->ps_nosuspend_wl);
#endif
 //   wake_lock_destroy(&ps_data->ps_wakelock);
    mutex_destroy(&ps_data->io_lock);
        kfree(ps_data);
    return err;
}


static int stk3x1x_remove(struct i2c_client *client)
{
        struct stk3x1x_data *ps_data = i2c_get_clientdata(client);
        device_init_wakeup(&client->dev, false);
#if (!defined(STK_POLL_ALS) || !defined(STK_POLL_PS))
        free_irq(ps_data->irq, ps_data);
        #ifdef SPREADTRUM_PLATFORM
               // sprd_free_gpio_irq(ps_data->int_pin);
               gpio_free(ps_data->int_pin);
        #else
                gpio_free(ps_data->int_pin);
        #endif
#endif  /* #if (!defined(STK_POLL_ALS) || !defined(STK_POLL_PS)) */


#ifdef STK_GES
        sysfs_remove_group(&ps_data->ges_input_dev->dev.kobj, &stk_ges_attribute_group);
        input_unregister_device(ps_data->ges_input_dev);
        input_free_device(ps_data->ges_input_dev);
#endif
#if (!defined(STK_POLL_ALS) || !defined(STK_POLL_PS))
        destroy_workqueue(ps_data->stk_wq);
#endif
        sysfs_remove_group(&ps_data->ps_input_dev->dev.kobj, &stk_ps_attribute_group);
        sysfs_remove_group(&ps_data->als_input_dev->dev.kobj, &stk_als_attribute_group);
        input_unregister_device(ps_data->ps_input_dev);
        input_unregister_device(ps_data->als_input_dev);
        input_free_device(ps_data->ps_input_dev);
        input_free_device(ps_data->als_input_dev);
#ifdef STK_QUALCOMM_POWER_CTRL
        stk3x1x_power_ctl(ps_data, false);
        stk3x1x_power_init(ps_data, false);
#endif
#ifdef STK_POLL_ALS
        hrtimer_try_to_cancel(&ps_data->als_timer);
        destroy_workqueue(ps_data->stk_als_wq);
#endif
#ifdef STK_TUNE0
        destroy_workqueue(ps_data->stk_ps_tune0_wq);
#endif
#ifdef STK_POLL_PS
        hrtimer_try_to_cancel(&ps_data->ps_timer);
        destroy_workqueue(ps_data->stk_ps_wq);
        wake_lock_destroy(&ps_data->ps_nosuspend_wl);
#endif
       // wake_lock_destroy(&ps_data->ps_wakelock);
    mutex_destroy(&ps_data->io_lock);
        kfree(ps_data);


		stk3x1x_i2c_client = NULL;
    return 0;
}

static const struct i2c_device_id stk_ps_id[] =
{
    { "stk3x1x_pls", 0},
    {}
};
MODULE_DEVICE_TABLE(i2c, stk_ps_id);

static struct of_device_id stk_match_table[] = {
        { .compatible = "stk,stk3x1x", },
        { },
};
MODULE_DEVICE_TABLE(of,stk_match_table);

static struct i2c_driver stk_ps_driver =
{
    .driver = {
        .name = DEVICE_NAME,
                .owner = THIS_MODULE,
#ifdef CONFIG_OF
                .of_match_table = stk_match_table,
#endif
                .pm = &stk3x1x_pm_ops,
    },
    .probe = stk3x1x_probe,
    .remove = stk3x1x_remove,
    .id_table = stk_ps_id,
};


static int __init stk3x1x_init(void)
{
        int ret;

        printk("stk3x1x_init");

    ret = i2c_add_driver(&stk_ps_driver);
    if (ret)
        {
                i2c_del_driver(&stk_ps_driver);
        return ret;
        }
    return 0;
}

static void __exit stk3x1x_exit(void)
{
    i2c_del_driver(&stk_ps_driver);
}

module_init(stk3x1x_init);
module_exit(stk3x1x_exit);
MODULE_AUTHOR("Lex Hsieh <lex_hsieh@sensortek.com.tw>");
MODULE_DESCRIPTION("Sensortek stk3x1x Proximity Sensor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);
