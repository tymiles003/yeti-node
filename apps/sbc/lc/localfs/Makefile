plug_in_name = lm_localfs
sbc_app_path = ../..

module_ldflags =
module_cflags  = -DMOD_NAME=\"$(plug_in_name)\"  -I$(sbc_app_path)

COREPATH =../../../../core
include $(COREPATH)/plug-in/Makefile.app_module
