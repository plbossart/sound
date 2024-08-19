// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2015-17 Intel Corporation.

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/types.h>
#include "internal.h"

struct regmap_async_sdw {
	struct regmap_async core;
	struct sdw_bpt_msg bpt_msg;
};

static void regmap_async_raw_write_complete(void *data)
{
	struct sdw_bpt_msg *bpt_msg = data;
	struct regmap_async_sdw *async = container_of(bpt_msg,
						      struct regmap_async_sdw,
						      bpt_msg);

	regmap_async_complete_cb(&async->core, bpt_msg->status);
}

static struct regmap_async *regmap_async_raw_write_alloc(void)
{
	struct regmap_async_sdw *async_sdw;

	async_sdw = kzalloc(sizeof(*async_sdw), GFP_KERNEL);
	if (!async_sdw)
		return NULL;

	async_sdw->bpt_msg.complete = regmap_async_raw_write_complete;

	return &async_sdw->core;
}

static int regmap_async_raw_write_lock(void *context, unsigned int reg, size_t val_len)
{
	struct device *dev = context;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);

	return sdw_async_raw_write_lock(slave, reg, val_len);
}

static int regmap_async_raw_write_unlock(void *context)
{
	struct device *dev = context;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);

	return sdw_async_raw_write_unlock(slave);
}

static int regmap_async_raw_write(void *context,
				  const void *reg, size_t reg_len,
				  const void *val, size_t val_len,
				  struct regmap_async *a)
{
	struct device *dev = context;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct regmap_async_sdw *async = container_of(a,
						      struct regmap_async_sdw,
						      core);
	
	if (val_len > SDW_BPT_MSG_MAX_BYTES)
               return -EINVAL;

       /* check device is enumerated */
       if (slave->dev_num == SDW_ENUM_DEV_NUM ||
           slave->dev_num > SDW_MAX_DEVICES)
               return -ENODEV;
	
       return sdw_async_raw_write(slave, &async->bpt_msg, reg, reg_len, val, val_len);
}

static int regmap_sdw_write(void *context, const void *val_buf, size_t val_size)
{
	struct device *dev = context;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	/* First word of buffer contains the destination address */
	u32 addr = le32_to_cpu(*(const __le32 *)val_buf);
	const u8 *val = val_buf;

	return sdw_nwrite_no_pm(slave, addr, val_size - sizeof(addr), val + sizeof(addr));
}

static int regmap_sdw_gather_write(void *context,
				   const void *reg_buf, size_t reg_size,
				   const void *val_buf, size_t val_size)
{
	struct device *dev = context;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	u32 addr = le32_to_cpu(*(const __le32 *)reg_buf);

	return sdw_nwrite_no_pm(slave, addr, val_size, val_buf);
}

static int regmap_sdw_read(void *context,
			   const void *reg_buf, size_t reg_size,
			   void *val_buf, size_t val_size)
{
	struct device *dev = context;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	u32 addr = le32_to_cpu(*(const __le32 *)reg_buf);

	return sdw_nread_no_pm(slave, addr, val_size, val_buf);
}

static const struct regmap_bus regmap_sdw = {
	.async_alloc = regmap_async_raw_write_alloc,
	.async_raw_write_lock =  regmap_async_raw_write_lock,
	.async_raw_write_unlock =  regmap_async_raw_write_unlock,
	.async_write = regmap_async_raw_write,
	.write = regmap_sdw_write,
	.gather_write = regmap_sdw_gather_write,
	.read = regmap_sdw_read,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

static int regmap_sdw_config_check(const struct regmap_config *config)
{
	/* Register addresses are 32 bits wide */
	if (config->reg_bits != 32)
		return -ENOTSUPP;

	if (config->pad_bits != 0)
		return -ENOTSUPP;

	/* Only bulk writes are supported not multi-register writes */
	if (config->can_multi_write)
		return -ENOTSUPP;

	return 0;
}

struct regmap *__regmap_init_sdw(struct sdw_slave *sdw,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name)
{
	int ret;

	ret = regmap_sdw_config_check(config);
	if (ret)
		return ERR_PTR(ret);

	return __regmap_init(&sdw->dev, &regmap_sdw,
			&sdw->dev, config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_sdw);

struct regmap *__devm_regmap_init_sdw(struct sdw_slave *sdw,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name)
{
	int ret;

	ret = regmap_sdw_config_check(config);
	if (ret)
		return ERR_PTR(ret);

	return __devm_regmap_init(&sdw->dev, &regmap_sdw,
			&sdw->dev, config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_sdw);

MODULE_DESCRIPTION("regmap SoundWire Module");
MODULE_LICENSE("GPL v2");
