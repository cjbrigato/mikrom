/*
 * This file is generated by gdbus-codegen, do not modify it.
 *
 * The license of this code is the same as for the D-Bus interface description
 * it was derived from. Note that it links to GLib, so must comply with the
 * LGPL linking clauses.
 */

#ifndef __META_DBUS_INPUT_MAPPING_H__
#define __META_DBUS_INPUT_MAPPING_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/* ------------------------------------------------------------------------ */
/* Declarations for org.gnome.Mutter.InputMapping */

#define META_DBUS_TYPE_INPUT_MAPPING (meta_dbus_input_mapping_get_type())
#define META_DBUS_INPUT_MAPPING(o)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((o), META_DBUS_TYPE_INPUT_MAPPING, \
                              MetaDBusInputMapping))
#define META_DBUS_IS_INPUT_MAPPING(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE((o), META_DBUS_TYPE_INPUT_MAPPING))
#define META_DBUS_INPUT_MAPPING_GET_IFACE(o)                        \
  (G_TYPE_INSTANCE_GET_INTERFACE((o), META_DBUS_TYPE_INPUT_MAPPING, \
                                 MetaDBusInputMappingIface))

struct _MetaDBusInputMapping;
typedef struct _MetaDBusInputMapping MetaDBusInputMapping;
typedef struct _MetaDBusInputMappingIface MetaDBusInputMappingIface;

struct _MetaDBusInputMappingIface {
  GTypeInterface parent_iface;

  gboolean (*handle_get_device_mapping)(MetaDBusInputMapping* object,
                                        GDBusMethodInvocation* invocation,
                                        const gchar* arg_device_node);
};

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MetaDBusInputMapping, g_object_unref)
#endif

GType meta_dbus_input_mapping_get_type(void) G_GNUC_CONST;

GDBusInterfaceInfo* meta_dbus_input_mapping_interface_info(void);
guint meta_dbus_input_mapping_override_properties(GObjectClass* klass,
                                                  guint property_id_begin);

/* D-Bus method call completion functions: */
void meta_dbus_input_mapping_complete_get_device_mapping(
    MetaDBusInputMapping* object,
    GDBusMethodInvocation* invocation,
    GVariant* rect);

/* D-Bus method calls: */
void meta_dbus_input_mapping_call_get_device_mapping(
    MetaDBusInputMapping* proxy,
    const gchar* arg_device_node,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean meta_dbus_input_mapping_call_get_device_mapping_finish(
    MetaDBusInputMapping* proxy,
    GVariant** out_rect,
    GAsyncResult* res,
    GError** error);

gboolean meta_dbus_input_mapping_call_get_device_mapping_sync(
    MetaDBusInputMapping* proxy,
    const gchar* arg_device_node,
    GVariant** out_rect,
    GCancellable* cancellable,
    GError** error);

/* ---- */

#define META_DBUS_TYPE_INPUT_MAPPING_PROXY \
  (meta_dbus_input_mapping_proxy_get_type())
#define META_DBUS_INPUT_MAPPING_PROXY(o)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((o), META_DBUS_TYPE_INPUT_MAPPING_PROXY, \
                              MetaDBusInputMappingProxy))
#define META_DBUS_INPUT_MAPPING_PROXY_CLASS(k)                      \
  (G_TYPE_CHECK_CLASS_CAST((k), META_DBUS_TYPE_INPUT_MAPPING_PROXY, \
                           MetaDBusInputMappingProxyClass))
#define META_DBUS_INPUT_MAPPING_PROXY_GET_CLASS(o)                    \
  (G_TYPE_INSTANCE_GET_CLASS((o), META_DBUS_TYPE_INPUT_MAPPING_PROXY, \
                             MetaDBusInputMappingProxyClass))
#define META_DBUS_IS_INPUT_MAPPING_PROXY(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE((o), META_DBUS_TYPE_INPUT_MAPPING_PROXY))
#define META_DBUS_IS_INPUT_MAPPING_PROXY_CLASS(k) \
  (G_TYPE_CHECK_CLASS_TYPE((k), META_DBUS_TYPE_INPUT_MAPPING_PROXY))

typedef struct _MetaDBusInputMappingProxy MetaDBusInputMappingProxy;
typedef struct _MetaDBusInputMappingProxyClass MetaDBusInputMappingProxyClass;
typedef struct _MetaDBusInputMappingProxyPrivate
    MetaDBusInputMappingProxyPrivate;

struct _MetaDBusInputMappingProxy {
  /*< private >*/
  GDBusProxy parent_instance;
  MetaDBusInputMappingProxyPrivate* priv;
};

struct _MetaDBusInputMappingProxyClass {
  GDBusProxyClass parent_class;
};

GType meta_dbus_input_mapping_proxy_get_type(void) G_GNUC_CONST;

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MetaDBusInputMappingProxy, g_object_unref)
#endif

void meta_dbus_input_mapping_proxy_new(GDBusConnection* connection,
                                       GDBusProxyFlags flags,
                                       const gchar* name,
                                       const gchar* object_path,
                                       GCancellable* cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
MetaDBusInputMapping* meta_dbus_input_mapping_proxy_new_finish(
    GAsyncResult* res,
    GError** error);
MetaDBusInputMapping* meta_dbus_input_mapping_proxy_new_sync(
    GDBusConnection* connection,
    GDBusProxyFlags flags,
    const gchar* name,
    const gchar* object_path,
    GCancellable* cancellable,
    GError** error);

void meta_dbus_input_mapping_proxy_new_for_bus(GBusType bus_type,
                                               GDBusProxyFlags flags,
                                               const gchar* name,
                                               const gchar* object_path,
                                               GCancellable* cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);
MetaDBusInputMapping* meta_dbus_input_mapping_proxy_new_for_bus_finish(
    GAsyncResult* res,
    GError** error);
MetaDBusInputMapping* meta_dbus_input_mapping_proxy_new_for_bus_sync(
    GBusType bus_type,
    GDBusProxyFlags flags,
    const gchar* name,
    const gchar* object_path,
    GCancellable* cancellable,
    GError** error);

/* ---- */

#define META_DBUS_TYPE_INPUT_MAPPING_SKELETON \
  (meta_dbus_input_mapping_skeleton_get_type())
#define META_DBUS_INPUT_MAPPING_SKELETON(o)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((o), META_DBUS_TYPE_INPUT_MAPPING_SKELETON, \
                              MetaDBusInputMappingSkeleton))
#define META_DBUS_INPUT_MAPPING_SKELETON_CLASS(k)                      \
  (G_TYPE_CHECK_CLASS_CAST((k), META_DBUS_TYPE_INPUT_MAPPING_SKELETON, \
                           MetaDBusInputMappingSkeletonClass))
#define META_DBUS_INPUT_MAPPING_SKELETON_GET_CLASS(o)                    \
  (G_TYPE_INSTANCE_GET_CLASS((o), META_DBUS_TYPE_INPUT_MAPPING_SKELETON, \
                             MetaDBusInputMappingSkeletonClass))
#define META_DBUS_IS_INPUT_MAPPING_SKELETON(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE((o), META_DBUS_TYPE_INPUT_MAPPING_SKELETON))
#define META_DBUS_IS_INPUT_MAPPING_SKELETON_CLASS(k) \
  (G_TYPE_CHECK_CLASS_TYPE((k), META_DBUS_TYPE_INPUT_MAPPING_SKELETON))

typedef struct _MetaDBusInputMappingSkeleton MetaDBusInputMappingSkeleton;
typedef struct _MetaDBusInputMappingSkeletonClass
    MetaDBusInputMappingSkeletonClass;
typedef struct _MetaDBusInputMappingSkeletonPrivate
    MetaDBusInputMappingSkeletonPrivate;

struct _MetaDBusInputMappingSkeleton {
  /*< private >*/
  GDBusInterfaceSkeleton parent_instance;
  MetaDBusInputMappingSkeletonPrivate* priv;
};

struct _MetaDBusInputMappingSkeletonClass {
  GDBusInterfaceSkeletonClass parent_class;
};

GType meta_dbus_input_mapping_skeleton_get_type(void) G_GNUC_CONST;

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MetaDBusInputMappingSkeleton, g_object_unref)
#endif

MetaDBusInputMapping* meta_dbus_input_mapping_skeleton_new(void);

G_END_DECLS

#endif /* __META_DBUS_INPUT_MAPPING_H__ */
