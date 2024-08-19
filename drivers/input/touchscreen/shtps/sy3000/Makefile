EXTRA_CFLAGS += -DSHTPS_FACTORY_MODE_ENABLE
EXTRA_CFLAGS += -DSHTPS_ENGINEER_BUILD_ENABLE

obj-y += shtps.o

shtps-y	+= 	shtps_spi.o				\
			shtps_i2c.o				\
			shtps_rmi.o				\
			shtps_rmi_sub.o			\
			shtps_rmi_ldm.o			\
			shtps_rmi_devctl.o		\
			shtps_rmi_debug.o

shtps-y	+= 	shtps_fwctl.o			\
			shtps_filter.o

shtps-y	+= 	filter/shtps_filter_dynamic_reset_check.o
shtps-y	+= 	fwctl/shtps_fwctl_s3400.o
shtps-y	+= 	fwctl/shtps_fwctl_s3700.o

