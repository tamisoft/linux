/*
 *  linux/drivers/video/ls013b7dh03fb.c
 *  Sharp LS013B7DH03 Memory LCD
 *
 *  Copyright (C) 2013 Levente Tamas (Navicron Oy)
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */


#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <linux/spi/ls013b7dh03.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <asm/delay.h>

#include <linux/fb.h>
#include <linux/init.h>

struct sharp_dev {
	u8 disp_on;
	struct spi_device	*spi;
	struct fb_info *info;
	struct mutex		lock;
};

// -- FRAMEBUFFER RELATED --
#define VIDEOMEMSIZE	(3*128*128)
#define DISP_BUFFER_SIZE (18*128+2) // 1 start transmission, 1 addr/line, 16 data/line, 1 eol/line, 1 end transmission

#define SWAPBITS(x) \
	(((x & 0x80) >> 7) | ((x & 0x40) >> 5) | ((x & 0x20) >> 3) | ((x & 0x10) >> 1) \
	| ((x & 0x08) << 1) | ((x & 0x04) << 3) | ((x & 0x02) << 5) | ((x & 0x01) << 7))

static void *videomemory;
static u_long videomemorysize=VIDEOMEMSIZE;
static u8 spi_tx_buf[DISP_BUFFER_SIZE];

ssize_t fbops_fb_write(struct fb_info *info, const char __user *buf, size_t count, loff_t *ppos);

static void *rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size);
	adr = (unsigned long) mem;
	while (size > 0) {
		SetPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return mem;
}

static void rvfree(void *mem, unsigned long size)
{
	unsigned long adr;

	if (!mem)
		return;

	adr = (unsigned long) mem;
	while ((long) size > 0) {
		ClearPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	vfree(mem);
}

static struct fb_var_screeninfo vfb_default = {
	.xres =		128,
	.yres =		128,
	.xres_virtual =	128,
	.yres_virtual =	128,
	.bits_per_pixel = 8,
	.red =		{ 0, 8, 0 },
   	.green =	{ 0, 8, 0 },
   	.blue =		{ 0, 8, 0 },
   	.activate =	FB_ACTIVATE_TEST,
   	.height =	20, // height in mm
   	.width =	20, // width in mm
   	.pixclock =	KHZ2PICOS((128*128*60)/1000), //101725 is closer, but gets displayed as 599 ->59.9Hz, lets display 59 instead :)
   	.left_margin =	0,
   	.right_margin =	0,
  	.upper_margin =	0,
   	.lower_margin =	0,
   	.hsync_len =	0,
   	.vsync_len =	0,
   	.vmode =	FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo vfb_fix = {
	.id =		"Sharp FB",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.xpanstep =	0,
	.ypanstep =	0,
	.ywrapstep =	0,
	.accel =	FB_ACCEL_NONE,
};

static struct fb_ops vfb_ops = {
	.fb_read        = fb_sys_read,
	.fb_write       = fbops_fb_write,
//	.fb_check_var	= vfb_check_var,
//	.fb_set_par	= vfb_set_par,
//	.fb_setcolreg	= vfb_setcolreg,
//	.fb_pan_display	= vfb_pan_display,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
//	.fb_mmap	= vfb_mmap,
};


static int spiops_write_spi(struct sharp_dev *par)
{
	struct spi_message message;
	struct spi_transfer *msg_buf;
	int ret = 0;

    if (!par->spi) {
        return -1;
    }
//    return spi_write(par->spi, spi_tx_buf, DISP_BUFFER_SIZE);

	msg_buf = kzalloc(sizeof(struct spi_transfer),
			GFP_KERNEL);
	if (!msg_buf)
		return -ENOMEM;

	spi_message_init(&message);

	msg_buf->tx_buf = spi_tx_buf;
	msg_buf->len = DISP_BUFFER_SIZE;
	msg_buf->cs_change=0;
	spi_message_add_tail(msg_buf, &message);

	ret = spi_sync(par->spi, &message);

	kfree(msg_buf);

	return ret;

}


static int __write_vmem_to_spi(struct sharp_dev *par)
{
    //FIXME: No need for u16*. Change to u8* and make things more pretty!
    u8 *vmem;
    int xres = 0;
    int yres = 0;
    int bpp=0;
    int line_bytes=0;
    u8 red,green,blue,tmp_store=0;
    u8 gray=0;
    unsigned char bwbit = 0;
    int x=0,y=0,ret=0;

    xres = par->info->var.xres;
    yres = par ->info->var.yres;
    bpp=par->info->var.red.length + par->info->var.green.length + par->info->var.blue.length;
    line_bytes=xres*bpp/8;

    // first line address
    vmem = (u8 *)(par->info->screen_base);
    
    // for each line we update
    for (y=0;y<yres;y++)
    {
    	tmp_store=0;
        for (x=0;x<xres;x++) {       
            red = vmem[y*line_bytes+x*3];
            green = vmem[y*line_bytes+x*3+1];
            blue = vmem[y*line_bytes+x*3+2];
            gray = red/3+green/2+blue/9; //pretty close to 30%R 59%G 11%B -> 33 50 11.11
            bwbit = (gray>=128)?1:0;
            if ((x%8==0)&&(x>0))
            {
                spi_tx_buf[2+(y*18)+(x/8)-1]=tmp_store;
                tmp_store=0;
            }
            tmp_store=tmp_store | (bwbit << (7-(x%8)));
       
        }
       spi_tx_buf[2+(y*18)+(x/8)-1]=tmp_store;
    }
    ret = spiops_write_spi(par);

    
    udelay(10);
    //printk(KERN_INFO "disp-on %d: 0\n",par->disp_on);
    gpio_set_value(par->disp_on, 0);
    udelay(10);
    gpio_set_value(par->disp_on, 1);
    //printk(KERN_INFO "disp-on %d: 1\n",par->disp_on);

    return ret;
}

void fbops_update_display(struct spi_device *dev)
{
    struct sharp_dev *lcddata;
    int ret = 0;
	lcddata = spi_get_drvdata(dev);
/* we do full updates
    // Sanity checks
    if (par->dirty_lines_start > par->dirty_lines_end) {
        par->dirty_lines_start = 0;
        par->dirty_lines_end = par->info->var.yres - 1;
    }
    if (par->dirty_lines_start > par->info->var.yres - 1 || par->dirty_lines_end > par->info->var.yres - 1) {
        par->dirty_lines_start = 0;
        par->dirty_lines_end = par->info->var.yres - 1;
    }

    if (par->sharpfbops.set_addr_win)
        par->sharpfbops.set_addr_win(par, 0, par->dirty_lines_start, par->info->var.xres-1, par->dirty_lines_end);
*/
    ret = __write_vmem_to_spi(lcddata);

    // set display line markers as clean
/*
      par->dirty_lines_start = par->info->var.yres - 1;
      par->dirty_lines_end = 0;
*/
}

void fbops_deferred_io(struct fb_info *info, struct list_head *pagelist)
{
/*
    struct sharpfb_par *par = info->par;
    struct page *page;
    unsigned long index;
    unsigned y_low=0, y_high=0;
    int count = 0;

    // Mark display lines as dirty
    list_for_each_entry(page, pagelist, lru) {
        count++;
        index = page->index << PAGE_SHIFT;
        y_low = index / info->fix.line_length;
        y_high = (index + PAGE_SIZE - 1) / info->fix.line_length;
        if (y_high > info->var.yres - 1)
            y_high = info->var.yres - 1;
        if (y_low < par->dirty_lines_start)
            par->dirty_lines_start = y_low;
        if (y_high > par->dirty_lines_end)
            par->dirty_lines_end = y_high;
    }
*/ 
    //we do full display updates
    fbops_update_display(info->par);
}
void fbops_mkdirty(struct fb_info *info, int y, int height)
{
    struct fb_deferred_io *fbdefio = info->fbdefio;

    // Schedule deferred_io to update display (no-op if already on queue)
    schedule_delayed_work(&info->deferred_work, fbdefio->delay);
}

ssize_t fbops_fb_write(struct fb_info *info, const char __user *buf, size_t count, loff_t *ppos)
{
//    struct sharpfb_par *par = info->par;
    ssize_t res;

    res = fb_sys_write(info, buf, count, ppos);

    // TODO: only mark changed area
    // update all for now
   fbops_mkdirty(info, -1, 0);

    return res;
}

static int ls013b7dh03_fb_probe(struct spi_device *dev)
{
	struct fb_info *info;
	int retval = -ENOMEM;
	struct sharp_dev *lcddata;
	struct fb_deferred_io *fbdefio = NULL;
	struct device *device=&dev->dev;
	
	lcddata = spi_get_drvdata(dev);

	/*
	 * For real video cards we use ioremap.
	 */
	printk(KERN_INFO "sharpfb: allocating %d bytes of video memory in system ram\n",(unsigned int)videomemorysize);

	if (!(videomemory = rvmalloc(videomemorysize)))
		return retval;

	/*
	 * VFB must clear memory to prevent kernel info
	 * leakage into userspace
	 * VGA-based drivers MUST NOT clear memory if
	 * they want to be able to take over vgacon
	 */
	printk(KERN_INFO "sharpfb: clearing %d bytes of video memory at 0x%08x\n",(unsigned int)videomemorysize,(unsigned int)videomemory);

	memset(videomemory, 0, videomemorysize);

	info = framebuffer_alloc(sizeof(struct fb_info), device);
	if (!info)
		goto err;
	printk(KERN_INFO "sharpfb: info allocated at 0x%08x dev->dev: 0x%08x\n",(unsigned int)info,(unsigned int)device);

	fbdefio = kzalloc(sizeof(struct fb_deferred_io), GFP_KERNEL);
    if (!fbdefio)
        goto err;

	printk(KERN_INFO "sharpfb: deferred io allocated at 0x%08x\n",(unsigned int)fbdefio);

    lcddata->info=info;

	info->screen_base = (char __iomem *)videomemory;
	info->fbops = &vfb_ops;
	info->fbdefio = fbdefio;

	fbdefio->delay = HZ/60; //60 fps
	fbdefio->deferred_io = fbops_deferred_io;
	fb_deferred_io_init(info);
/*	retval = fb_find_mode(&info->var, info, NULL,
			      NULL, 0, NULL, 8);

	printk(KERN_INFO "sharpfb: fb_find_mode retval: 0x%01x\n",retval);

	if (!retval || (retval == 4))
		{
			printk(KERN_INFO "sharpfb: using default mode\n");
			info->var = vfb_default;
		}
*/
	info->var = vfb_default;
	vfb_fix.smem_start = (unsigned long) videomemory;
	vfb_fix.smem_len = videomemorysize;
	info->fix = vfb_fix;
	info->pseudo_palette = info->par;
	info->par = dev;
	info->flags = FBINFO_FLAG_DEFAULT;
	//info->device = (struct device *)parent;

    
	printk(KERN_INFO "sharpfb: fb_alloc_cmap at: 0x%08x\n",(unsigned int)&info->cmap);
	
	retval = fb_alloc_cmap(&info->cmap, 256, 0);
	if (retval < 0)
		goto err1;

	printk(KERN_INFO "sharpfb: register framebuffer\n");
	retval = register_framebuffer(info);
	printk(KERN_INFO "sharpfb: register framebuffer retval: %d\n",retval);
	if (retval < 0)
		goto err2;
	printk(KERN_INFO "sharpfb: platform_set_drvdata dev: 0x%08x info: 0x%08x\n",(unsigned int)dev,(unsigned int)info);
	//spi_set_drvdata(dev, info);

	printk(KERN_INFO
	       "fb%d: Virtual frame buffer device, using %ldK of video memory\n",
	       info->node, videomemorysize >> 10);
	return 0;
err2:
	printk(KERN_INFO "sharpfb: err2\n");
	fb_dealloc_cmap(&info->cmap);
err1:
	printk(KERN_INFO "sharpfb: err1\n");
	framebuffer_release(info);
err:
	printk(KERN_INFO "sharpfb: err\n");
	rvfree(videomemory, videomemorysize);
	return retval;
}

static int ls013b7dh03_fb_remove(struct fb_info *info)
{
//	struct fb_info *info = platform_get_drvdata(dev);

	if (info) {
		fb_deferred_io_cleanup(info);
		unregister_framebuffer(info);
		rvfree(videomemory, videomemorysize);
		fb_dealloc_cmap(&info->cmap);
		kfree(info->fbdefio);
		framebuffer_release(info);
	}

	return 0;
}



static int ls013b7dh03_probe(struct spi_device *spi)
{
	struct sharp_dev *lcd;
//	struct sharp_dev_platform_data *pdata;
	const struct of_device_id *match;
	int ret,i=0;

	if (!spi->dev.of_node) {
		dev_err(&spi->dev, "No device tree data available.\n");
		return -EINVAL;
	}

	/*
	 * bits_per_word cannot be configured in platform data
	 */
	/*spi->bits_per_word = 8;
	spi->mode=SPI_MODE_0 | SPI_CS_HIGH;
	spi->chip_select=1;
	*/
	printk(KERN_INFO "sharpfb: spi_setup\n");
	ret = spi_setup(spi);
	if (ret < 0)
		return ret;
	printk(KERN_INFO "sharpfb: device match SPI of_node: %s fn: %s\n",spi->dev.of_node->name,spi->dev.of_node->full_name);
	
	// see if we have a matching DTS entry
	match = of_match_device(ls013b7dh03_dt_ids, &spi->dev);
	if (match == NULL)
		printk(KERN_INFO "sharpfb: device match SPI returned NULL\n");
		
	if (!match ) //|| !match->data) removed, since in this file we don't return data for the driver
		return -EINVAL;

	printk(KERN_INFO "sharpfb: dev struct malloc\n");

	lcd = devm_kzalloc(&spi->dev, sizeof(*lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	mutex_init(&lcd->lock);

	printk(KERN_INFO "sharpfb: set drvdata\n");

	spi_set_drvdata(spi, lcd);

	lcd->spi = spi;

	printk(KERN_INFO "sharpfb: GPIO disp-on setup\n");

	lcd->disp_on = of_get_named_gpio(spi->dev.of_node, "gpios-disp-on", 0);
	if (!gpio_is_valid(lcd->disp_on)) {
		dev_err(&spi->dev, "Missing dt property: gpios-disp-on\n");
		return -EINVAL;
	}

	ret = devm_gpio_request_one(&spi->dev, lcd->disp_on,
				    GPIOF_OUT_INIT_HIGH,
				    "ls013b7dh03-disp-on");
	if (ret) {
		dev_err(&spi->dev,
			"failed to request gpio %d: %d\n",
			lcd->disp_on,ret);
		return -EINVAL;
	}
/*
	printk(KERN_INFO "sharpfb: framebuffer device alloc\n");
	
	// Everything seems ok, let's try to create the framebuffer
	lcd->fbdev = platform_device_alloc("ls013b7dh03fb", 0);

	if (lcd->fbdev)
		ret = platform_device_add(lcd->fbdev);
	else
		ret = -ENOMEM;	
	
	if (ret) {
		platform_device_put(lcd->fbdev);
		goto exit_destroy;
	}

	printk(KERN_INFO "sharpfb: framebuffer device setup\n");
*/	
    spi_tx_buf[0] = 0x80; // Config bits
    
    for (i=0;i<128;i++) {
        spi_tx_buf[(1+(i*18))] = SWAPBITS( (i+1) ); // Address lines 
        spi_tx_buf[(18+(i*18))] = 0x0; // Line end dummies
    }
    spi_tx_buf[DISP_BUFFER_SIZE-1] = 0x0; // Data end dummies

	ret = ls013b7dh03_fb_probe(spi); //lcd->fbdev
	
	if (ret)
		goto exit_destroy;

	return ret;

exit_destroy:
	spi_set_drvdata(spi, NULL);
	mutex_destroy(&lcd->lock);
	return ret;
}

static int ls013b7dh03_remove(struct spi_device *spi)
{
	struct sharp_dev *lcd;
	int ret=0;

	lcd = spi_get_drvdata(spi);
	if (lcd == NULL)
		return -ENODEV;

	ret = ls013b7dh03_fb_remove(lcd->info);

	spi_set_drvdata(spi, NULL);

	
	if (!ret)
		mutex_destroy(&lcd->lock);
	else
		dev_err(&spi->dev, "Failed to remove the spi display: %d\n", ret);

	return ret;
}

static const struct of_device_id ls013b7dh03_dt_ids[]= {
	{
		.compatible = "sharp,ls013b7dh03",
//		.data = ls013b7dh03_lcd_init,
	},
	{},
};
MODULE_DEVICE_TABLE(of, ls013b7dh03_dt_ids);

static struct spi_driver ls013b7dh03_driver = {
	.probe  = ls013b7dh03_probe,
	.remove = ls013b7dh03_remove,
	.driver = {
		.name = "ls013b7dh03",
		.of_match_table = of_match_ptr(ls013b7dh03_dt_ids),
	},
};

module_spi_driver(ls013b7dh03_driver);

MODULE_AUTHOR("Levente Tamas <levente.tamas@navicron.com>");
MODULE_DESCRIPTION("Sharp LS013B7DH03 memory LCD Driver");
MODULE_LICENSE("GPL");
