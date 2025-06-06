
/* This file is generated by glib-mkenums, do not modify it. This code is licensed under the same license as the containing project. Note that it links to GLib, so must comply with the LGPL linking clauses. */

#include <gudev.h>
/* enumerations from "gudevenums.h" */
GType
g_udev_device_type_get_type (void)
{
  static gsize static_g_define_type_id = 0;

  if (g_once_init_enter (&static_g_define_type_id))
    {
      static const GEnumValue values[] = {
        { G_UDEV_DEVICE_TYPE_NONE, "G_UDEV_DEVICE_TYPE_NONE", "none" },
        { G_UDEV_DEVICE_TYPE_BLOCK, "G_UDEV_DEVICE_TYPE_BLOCK", "block" },
        { G_UDEV_DEVICE_TYPE_CHAR, "G_UDEV_DEVICE_TYPE_CHAR", "char" },
        { 0, NULL, NULL }
      };
      GType g_define_type_id =
        g_enum_register_static (g_intern_static_string ("GUdevDeviceType"), values);
      g_once_init_leave (&static_g_define_type_id, g_define_type_id);
    }

  return static_g_define_type_id;
}

/* Generated data ends here */

