# BME280

A Linux device driver for the Bosch BME280 temperature, pressure, humidity sensor using the I2C protocol, written in C.

<img src="https://user-images.githubusercontent.com/28817028/210186916-9eb196d5-eaf1-4856-9faa-2ae71ba2e0b0.png" width=50% height=50%>


## Setup on Raspberry Pi




This driver was tested on a Raspberry Pi 4B. To run on Raspberry Pi OS:

Run `raspi config` and enable I2C from `Interface Options -> I2C`.

Hook up the BME280 sensor to the Raspberry Pi and run `i2cdetect` to verify
that it is available to interface:
```
sudo apt install i2c-tools # if not included
i2cdetect -y 1
```

The output of `i2cdetect` should be something like
```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
70: -- -- -- -- -- -- 76 --
```
Which indicates that the sensor is available at 0x76.

## Building and reading data

Build the driver:
```
cd driver && make
```

Upon successfully building, the kernel module `bme280.ko` will be created.

To initialize the driver, run:
```
sudo insmod bme280.ko
```

A device file should now be created in `/dev/bme280`. You can read continuous measurements
as a stream by reading the device file:
```
chmod 666 /dev/bme280 # enable read access
cat < /dev/bme280
```

To remove the driver, run
```
sudo rmmod bme280
```

## Data format
The data format of a single reading will be given as a single line in order of temperature, pressure, and
humidity:
```
T2920P24874300H27597
```

To interpret this line:
- Temperature is in Celsius and is accurate to two decimal places: e.g.
  `2920->29.20C`
- Pressure is in Pa and is obtained by dividing the value by 256, accurate to
  two decimal places, e.g. `24874300/256=97165.23 Pa`
- Humidity is in percentage and is obtained by dividng the value by 1024, e.g.
  `27597/1024=25.9%`

## Running the example

The userspace example is a simple Rust program that reads the driver file as a stream and parses it. The driver is expected to be initialized already, or else reading the driver file will not work. To start it, run
```
cd user/ && cargo run
```

Here is a line of expected console output:
```
Temperature: 29.23C Pressure: 97290.80Pa Humidity: 26.35%
```

## Running the IOCTL example

The IOCTL example is a simple C program that reads the driver file and uses the ioctl interface to read the sensor data (temperature, humidity and pressure) separately instead of reading it in one line. Like in the Rust example, the driver is expected to be initialized already, or else reading the driver file will not work. To start it, run

```
cd ioctl_example/ && make
./bme280
```

Here is a line of expected console output:
```
Temperature: 29.720000 C
Humidity: 42.366211 %
Pressure: 96080.445312 Pa
```
