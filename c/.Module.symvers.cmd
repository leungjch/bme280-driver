cmd_/home/pi/Documents/bme280/c/Module.symvers := sed 's/\.ko$$/\.o/' /home/pi/Documents/bme280/c/modules.order | scripts/mod/modpost -m -a  -o /home/pi/Documents/bme280/c/Module.symvers -e -i Module.symvers   -T -