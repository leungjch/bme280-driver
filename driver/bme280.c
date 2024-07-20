#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <asm/div64.h>
#include <linux/uaccess.h>
#include <linux/version.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("leungjch");
MODULE_DESCRIPTION("bme280 sensor driver");

#define DEV_NAME "bme280"
#define DEV_CLASS "BME280Class"

static struct i2c_adapter *bme280_i2c_adapter = NULL;
static struct i2c_client *bme280_i2c_client = NULL;

#define I2C_BUS_AVAILABLE 1        // i2c bus available on RPI
#define SLAVE_DEVICE_NAME "BME280" // device and driver name
#define BME280_SLAVE_ADDRESS 0x76  // i2c address

static struct i2c_driver bme280_i2c_driver = {
    .driver = {
        .name = SLAVE_DEVICE_NAME,
        .owner = THIS_MODULE}};

static struct i2c_board_info bme280_i2c_board_info = {
    I2C_BOARD_INFO(SLAVE_DEVICE_NAME, BME280_SLAVE_ADDRESS)};

dev_t dev_num = 0;
static struct class *dev_class;
static struct cdev bme280_cdev;

#define IOCTL_GET_TEMPERATURE _IOR('t', 1, long int)
#define IOCTL_GET_HUMIDITY    _IOR('h', 2, long int)
#define IOCTL_GET_PRESSURE    _IOR('p', 3, long int)

static int bme280_open(struct inode *device_file, struct file *instance);
static int bme280_close(struct inode *device_file, struct file *instance);
static ssize_t bme280_read(struct file *fptr, char __user *buf, size_t len, loff_t *off);

void read_calibration(void);
long signed int read_temperature(void);
long unsigned int read_pressure_int64(void);
long signed int read_humidity(void);

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#define CONCAT_BYTES(msb, lsb) (((uint16_t)msb << 8) | (uint16_t)lsb)

// calibration registers, read at initialization
// temperature calibration
unsigned short dig_T1;
signed short dig_T2;
signed short dig_T3;
unsigned short dig_P1;
signed short dig_P2;
signed short dig_P3;
signed short dig_P4;
signed short dig_P5;
signed short dig_P6;
signed short dig_P7;
signed short dig_P8;
signed short dig_P9;
unsigned char dig_H1;
signed short dig_H2;
unsigned char dig_H3;
signed short dig_H4;
signed short dig_H5;
signed char dig_H6;

// fine temperature, a global variable measured by read_temperature() and used
// by read_pressure()
long signed int t_fine;

// Returns pressure in Q24.8 format, divide by 256 to obtain answer accurate to
// 1 decimal place (e.g. 24674867/256=96386.2Pa)
// See p25 of datasheet for calculation formula
long unsigned int read_pressure_int64(void)
{
  long long signed int var1, var2, p;
  long long unsigned int var4;
  long signed int adc_P;

  u8 p1, p2, p3;
  p1 = (u8)(0xFF & i2c_smbus_read_byte_data(bme280_i2c_client, 0xF7));
  p2 = (u8)(0xFF & i2c_smbus_read_byte_data(bme280_i2c_client, 0xF8));
  p3 = (u8)(0xFF & i2c_smbus_read_byte_data(bme280_i2c_client, 0xF9));

  adc_P = ((p1 << 16) | (p2 << 8) | p3) >> 4;

  var1 = ((long long signed int)t_fine) - 128000;
  var2 = var1 * var1 * (long long signed int)dig_P6;
  var2 = var2 + ((var1 * (long long signed int)dig_P5) << 17);
  var2 = var2 + (((long long signed int)dig_P4) << 35);
  var1 = ((var1 * var1 * (long long signed int)dig_P3) >> 8) + ((var1 * (long long signed int)dig_P2) << 12);
  var1 = (((((long long signed int)1) << 47) + var1)) * ((long long signed int)dig_P1) >> 33;
  if (var1 == 0)
  {
    return 0;
  }
  p = 1048576 - adc_P;
  p = (((p << 31) - var2) * 3125);
  var4 = do_div(p, var1);
  var1 = (((long long signed int)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  var2 = (((long long signed int)dig_P8) * p) >> 19;
  p = ((p + var1 + var2) >> 8) + (((long long signed int)dig_P7) << 4);
  return (long unsigned int)p;
}

// Returns temperature in Celsius with 0.01 resolution, e.g. 3001 -> 30.01degC
// See p50 of datasheet for calculation formula
long signed int read_temperature(void)
{
  int var1, var2;
  long signed int adc_T;
  long signed int T;
  long signed int d1, d2, d3;

  // read temperature registers (see p31)
  d1 = i2c_smbus_read_byte_data(bme280_i2c_client, 0xFA);
  d2 = i2c_smbus_read_byte_data(bme280_i2c_client, 0xFB);
  d3 = i2c_smbus_read_byte_data(bme280_i2c_client, 0xFC);

  adc_T = ((d1 << 16) | (d2 << 8) | d3) >> 4;

  var1 = ((((adc_T >> 3) - (dig_T1 << 1))) * (dig_T2)) >> 11;
  var2 = (((((adc_T >> 4) - (dig_T1)) * ((adc_T >> 4) - (dig_T1))) >> 12) * (dig_T3)) >> 14;

  t_fine = (var1 + var2);
  T = (t_fine * 5 + 128) >> 8;
  return T;
}

long signed int read_humidity(void)
{
  long signed int adc_H;
  long signed int h1, h2;
  long signed int v_x1_u32r;

  // read temperature registers (see p31)
  h1 = (u8)(0xFF & i2c_smbus_read_byte_data(bme280_i2c_client, 0xFD));
  h2 = (u8)(0xFF & i2c_smbus_read_byte_data(bme280_i2c_client, 0xFE));

  adc_H = (h1 << 8) | h2;

  v_x1_u32r = (t_fine - ((long signed int)76800));
  v_x1_u32r = (((((adc_H << 14) - (((long signed int)dig_H4) << 20) - (((s32)dig_H5) * v_x1_u32r)) + ((long signed int)16384)) >> 15) * (((((((v_x1_u32r *
              ((long signed int)dig_H6)) >> 10) * (((v_x1_u32r * ((s32)dig_H3)) >> 11) + ((long signed int)32768))) >> 10) + ((s32)2097152)) * ((s32)dig_H2) + 8192) >> 14));
  v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((long signed int)dig_H1)) >> 4));
  v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
  v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
  return (long signed int)(v_x1_u32r >> 12);
}

void read_calibration(void)
{

// read registers
#define DATA1_LEN 24
#define DATA1_START 0x88
#define DATA2_LEN 8
#define DATA2_START 0xE1
  u8 reg_data1[DATA1_LEN];
  u8 reg_data2[DATA2_LEN];
  long signed int data_read;
  int16_t dig_h4_msb;
  int16_t dig_h4_lsb;
  int16_t dig_h5_msb;
  int16_t dig_h5_lsb;
  int i;
  i = 0;
  while (i < DATA1_LEN)
  {
    data_read = i2c_smbus_read_word_data(bme280_i2c_client, DATA1_START + i);
    reg_data1[i] = (u8)(data_read & 0xFF);
    i++;
  }
  i = 0;
  while (i < DATA2_LEN)
  {
    data_read = i2c_smbus_read_word_data(bme280_i2c_client, DATA2_START + i);
    reg_data2[i] = (u8)(data_read & 0xFF);
    i++;
  }

  // read calibration temperature registers (p. 24 of datasheet)
  dig_T1 = CONCAT_BYTES(reg_data1[1], reg_data1[0]);
  dig_T2 = CONCAT_BYTES(reg_data1[3], reg_data1[2]);
  dig_T3 = CONCAT_BYTES(reg_data1[5], reg_data1[4]);

  // read calibration pressure registers (p. 24 of datasheet)
  dig_P1 = CONCAT_BYTES(reg_data1[7], reg_data1[6]);
  dig_P2 = CONCAT_BYTES(reg_data1[9], reg_data1[8]);
  dig_P3 = CONCAT_BYTES(reg_data1[11], reg_data1[10]);
  dig_P4 = CONCAT_BYTES(reg_data1[13], reg_data1[12]);
  dig_P5 = CONCAT_BYTES(reg_data1[15], reg_data1[14]);
  dig_P6 = CONCAT_BYTES(reg_data1[17], reg_data1[16]);
  dig_P7 = CONCAT_BYTES(reg_data1[19], reg_data1[18]);
  dig_P8 = CONCAT_BYTES(reg_data1[21], reg_data1[20]);
  dig_P9 = CONCAT_BYTES(reg_data1[23], reg_data1[22]);

  // read calibration humidity registers
  dig_H1 = i2c_smbus_read_byte_data(bme280_i2c_client, 0xA1);
  dig_H2 = CONCAT_BYTES(reg_data2[1], reg_data2[0]);
  dig_H3 = reg_data2[2];

  dig_h4_msb = (int16_t)(int8_t)reg_data2[3] * 16;
  dig_h4_lsb = (int16_t)(reg_data2[4] & 0x0F);
  dig_H4 = dig_h4_msb | dig_h4_lsb;

  dig_h5_msb = (int16_t)(int8_t)reg_data2[5] * 16;
  dig_h5_lsb = (int16_t)((reg_data2[4] & 0xF0) >> 4);
  dig_H5 = dig_h5_msb | dig_h5_lsb;
  dig_H6 = reg_data2[6];
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = bme280_open,
    .release = bme280_close,
    .read = bme280_read,
    .unlocked_ioctl = device_ioctl
};

static int bme280_open(struct inode *device_file, struct file *instance)
{
  pr_info("bme280: open was called\n");
  return 0;
}

static int bme280_close(struct inode *device_file, struct file *instance)
{
  pr_info("bme280: close was called\n");
  return 0;
}

static ssize_t bme280_read(struct file *fptr, char __user *buf, size_t len, loff_t *off)
{
  long signed int temp;
  long signed int pressure64;
  long signed int humidity;

  read_calibration();

  temp = read_temperature();
  pressure64 = read_pressure_int64();
  humidity = read_humidity();

  char tempStr[11];
  char pressureStr[11];
  char humidityStr[11];
  char retStr[31];
  int copyRes;

  // copy numbers into string
  sprintf(tempStr, "%ld", temp);
  sprintf(pressureStr, "%lu", pressure64);
  sprintf(humidityStr, "%ld", humidity);

  strcpy(retStr, "T");
  strcat(retStr, tempStr);
  strcat(retStr, "P");
  strcat(retStr, pressureStr);
  strcat(retStr, "H");
  strcat(retStr, humidityStr);
  strcat(retStr, "\n");

  copyRes = copy_to_user(buf, retStr, strlen(retStr));
  return strlen(retStr);

  // pr_info("bme280 str: read temp: %s pressure: %s humidity: %s\n", tempStr, pressureStr, humidityStr);
  return 0;
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
  long int value;

  read_calibration();

  switch (cmd) {
    case IOCTL_GET_TEMPERATURE:
      value = read_temperature();
      if (copy_to_user((long int __user *)arg, &value, sizeof(value))) {
        return -EFAULT;
      }
      break;
    case IOCTL_GET_HUMIDITY:
      value = read_humidity();
      if (copy_to_user((long int __user *)arg, &value, sizeof(value))) {
        return -EFAULT;
      }
      break;
    case IOCTL_GET_PRESSURE:
      value = read_pressure_int64();
      if (copy_to_user((long int __user *)arg, &value, sizeof(value))) {
        return -EFAULT;
      }
      break;
    default:
      return -EINVAL;
  }
  return 0;
}

static int init_driver(void)
{
  int ret = -1;
  u8 id;
  pr_info("bme280: initializing...\n");

  // Allocate major number
  if (alloc_chrdev_region(&dev_num, 0, 1, DEV_NAME) < 0)
  {
    pr_err("bme280: could not allocate major number\n");
    return -1;
  }

  // Create class
  #if LINUX_VERSION_CODE <= KERNEL_VERSION(6,3,0)
  dev_class = class_create(THIS_MODULE, DEV_CLASS);
  #else
  dev_class = class_create(DEV_CLASS);
  #endif
  if (IS_ERR(dev_class))
  {
    pr_err("bme280: failed to create class\n");
    goto ClassError;
  }

  // Create device
  if (IS_ERR(device_create(dev_class, NULL, dev_num, NULL, DEV_NAME)))
  {
    pr_err("bme280: failed to create device\n");
    goto DeviceError;
  }

  // Initialize cdev and file operations on device file
  cdev_init(&bme280_cdev, &fops);

  // Add char device to system
  if (cdev_add(&bme280_cdev, dev_num, 1) == -1)
  {
    pr_err("bme280: failed to add device");
    goto KernelError;
  }

  // initialize i2c
  // client may already be registered. If so, skip
  bme280_i2c_adapter = i2c_get_adapter(I2C_BUS_AVAILABLE);
  if (bme280_i2c_adapter != NULL)
  {
    bme280_i2c_client = i2c_new_client_device(bme280_i2c_adapter, &bme280_i2c_board_info);
    if (bme280_i2c_client != NULL)
    {

      int add_i2c = i2c_add_driver(&bme280_i2c_driver);
      if (add_i2c != -1)
      {
        ret = 0;
      }
      else
      {
        pr_err("bme250: failed to add i2c driver \n");
      }
    }
    i2c_put_adapter(bme280_i2c_adapter);
  }

  // read id, which is always 0x60 for bme280
  id = i2c_smbus_read_byte_data(bme280_i2c_client, 0xD0);

  if (id != 0x60)
  {
    pr_err("bme280: id is not 0x60\n");
    goto I2CError;
  }

  pr_info("id is 0x%x\n", id);


  // set ctrl_hum (humidity oversampling)
  i2c_smbus_write_byte_data(bme280_i2c_client, 0xf2, (0x3 << 0));

  // set ctrl_meas register at 0xF4
  // set temperature oversampling to max (b101)
  // set pressure oversampling to max (b101)
  // set normal mode (b11)
  i2c_smbus_write_byte_data(bme280_i2c_client, 0xf4, (0x5 << 5) | (0x5 << 2) | (0x3 << 0));

  // set config register at 0xF5
  // set t_standby time
  i2c_smbus_write_byte_data(bme280_i2c_client, 0xf5, 0x3 << 5);

  // set the calibration registers
  read_calibration();

  pr_info("bme280: successfully init module\n");

  return ret;
I2CError:
  i2c_unregister_device(bme280_i2c_client);
  i2c_del_driver(&bme280_i2c_driver);
  cdev_del(&bme280_cdev);
KernelError:
  device_destroy(dev_class, dev_num);
DeviceError:
  class_destroy(dev_class);
ClassError:
  unregister_chrdev_region(dev_num, 1);
  return -1;
}

static void exit_driver(void)
{
  i2c_unregister_device(bme280_i2c_client);
  i2c_del_driver(&bme280_i2c_driver);
  cdev_del(&bme280_cdev);
  device_destroy(dev_class, dev_num);
  class_destroy(dev_class);
  unregister_chrdev_region(dev_num, 1);
  pr_info("bme280: successfully removed module\n");
  return;
}

module_init(init_driver);
module_exit(exit_driver);
