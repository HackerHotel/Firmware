# Emulating the badge module
#
# The badge module is a C module with Python bindings
# on the real badge, but for the emulator it's just a
# plain python module.

def nvs_get_u16(namespace, key, value):
    return value

def eink_init():
    "ok"

def safe_mode():
    return False
