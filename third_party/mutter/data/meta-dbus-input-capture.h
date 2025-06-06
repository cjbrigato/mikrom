/*
 * This file is generated by gdbus-codegen, do not modify it.
 *
 * The license of this code is the same as for the D-Bus interface description
 * it was derived from. Note that it links to GLib, so must comply with the
 * LGPL linking clauses.
 */

#ifndef __META_DBUS_INPUT_CAPTURE_H__
#define __META_DBUS_INPUT_CAPTURE_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/* ------------------------------------------------------------------------ */
/* Declarations for org.gnome.Mutter.InputCapture */

#define META_DBUS_TYPE_INPUT_CAPTURE (meta_dbus_input_capture_get_type())
#define META_DBUS_INPUT_CAPTURE(o)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((o), META_DBUS_TYPE_INPUT_CAPTURE, \
                              MetaDBusInputCapture))
#define META_DBUS_IS_INPUT_CAPTURE(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE((o), META_DBUS_TYPE_INPUT_CAPTURE))
#define META_DBUS_INPUT_CAPTURE_GET_IFACE(o)                        \
  (G_TYPE_INSTANCE_GET_INTERFACE((o), META_DBUS_TYPE_INPUT_CAPTURE, \
                                 MetaDBusInputCaptureIface))

struct _MetaDBusInputCapture;
typedef struct _MetaDBusInputCapture MetaDBusInputCapture;
typedef struct _MetaDBusInputCaptureIface MetaDBusInputCaptureIface;

struct _MetaDBusInputCaptureIface {
  GTypeInterface parent_iface;

  gboolean (*handle_create_session)(MetaDBusInputCapture* object,
                                    GDBusMethodInvocation* invocation,
                                    guint arg_capabilities);

  guint (*get_supported_capabilities)(MetaDBusInputCapture* object);
};

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MetaDBusInputCapture, g_object_unref)
#endif

GType meta_dbus_input_capture_get_type(void) G_GNUC_CONST;

GDBusInterfaceInfo* meta_dbus_input_capture_interface_info(void);
guint meta_dbus_input_capture_override_properties(GObjectClass* klass,
                                                  guint property_id_begin);

/* D-Bus method call completion functions: */
void meta_dbus_input_capture_complete_create_session(
    MetaDBusInputCapture* object,
    GDBusMethodInvocation* invocation,
    const gchar* session_path);

/* D-Bus method calls: */
void meta_dbus_input_capture_call_create_session(MetaDBusInputCapture* proxy,
                                                 guint arg_capabilities,
                                                 GCancellable* cancellable,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data);

gboolean meta_dbus_input_capture_call_create_session_finish(
    MetaDBusInputCapture* proxy,
    gchar** out_session_path,
    GAsyncResult* res,
    GError** error);

gboolean meta_dbus_input_capture_call_create_session_sync(
    MetaDBusInputCapture* proxy,
    guint arg_capabilities,
    gchar** out_session_path,
    GCancellable* cancellable,
    GError** error);

/* D-Bus property accessors: */
guint meta_dbus_input_capture_get_supported_capabilities(
    MetaDBusInputCapture* object);
void meta_dbus_input_capture_set_supported_capabilities(
    MetaDBusInputCapture* object,
    guint value);

/* ---- */

#define META_DBUS_TYPE_INPUT_CAPTURE_PROXY \
  (meta_dbus_input_capture_proxy_get_type())
#define META_DBUS_INPUT_CAPTURE_PROXY(o)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((o), META_DBUS_TYPE_INPUT_CAPTURE_PROXY, \
                              MetaDBusInputCaptureProxy))
#define META_DBUS_INPUT_CAPTURE_PROXY_CLASS(k)                      \
  (G_TYPE_CHECK_CLASS_CAST((k), META_DBUS_TYPE_INPUT_CAPTURE_PROXY, \
                           MetaDBusInputCaptureProxyClass))
#define META_DBUS_INPUT_CAPTURE_PROXY_GET_CLASS(o)                    \
  (G_TYPE_INSTANCE_GET_CLASS((o), META_DBUS_TYPE_INPUT_CAPTURE_PROXY, \
                             MetaDBusInputCaptureProxyClass))
#define META_DBUS_IS_INPUT_CAPTURE_PROXY(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE((o), META_DBUS_TYPE_INPUT_CAPTURE_PROXY))
#define META_DBUS_IS_INPUT_CAPTURE_PROXY_CLASS(k) \
  (G_TYPE_CHECK_CLASS_TYPE((k), META_DBUS_TYPE_INPUT_CAPTURE_PROXY))

typedef struct _MetaDBusInputCaptureProxy MetaDBusInputCaptureProxy;
typedef struct _MetaDBusInputCaptureProxyClass MetaDBusInputCaptureProxyClass;
typedef struct _MetaDBusInputCaptureProxyPrivate
    MetaDBusInputCaptureProxyPrivate;

struct _MetaDBusInputCaptureProxy {
  /*< private >*/
  GDBusProxy parent_instance;
  MetaDBusInputCaptureProxyPrivate* priv;
};

struct _MetaDBusInputCaptureProxyClass {
  GDBusProxyClass parent_class;
};

GType meta_dbus_input_capture_proxy_get_type(void) G_GNUC_CONST;

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MetaDBusInputCaptureProxy, g_object_unref)
#endif

void meta_dbus_input_capture_proxy_new(GDBusConnection* connection,
                                       GDBusProxyFlags flags,
                                       const gchar* name,
                                       const gchar* object_path,
                                       GCancellable* cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
MetaDBusInputCapture* meta_dbus_input_capture_proxy_new_finish(
    GAsyncResult* res,
    GError** error);
MetaDBusInputCapture* meta_dbus_input_capture_proxy_new_sync(
    GDBusConnection* connection,
    GDBusProxyFlags flags,
    const gchar* name,
    const gchar* object_path,
    GCancellable* cancellable,
    GError** error);

void meta_dbus_input_capture_proxy_new_for_bus(GBusType bus_type,
                                               GDBusProxyFlags flags,
                                               const gchar* name,
                                               const gchar* object_path,
                                               GCancellable* cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);
MetaDBusInputCapture* meta_dbus_input_capture_proxy_new_for_bus_finish(
    GAsyncResult* res,
    GError** error);
MetaDBusInputCapture* meta_dbus_input_capture_proxy_new_for_bus_sync(
    GBusType bus_type,
    GDBusProxyFlags flags,
    const gchar* name,
    const gchar* object_path,
    GCancellable* cancellable,
    GError** error);

/* ---- */

#define META_DBUS_TYPE_INPUT_CAPTURE_SKELETON \
  (meta_dbus_input_capture_skeleton_get_type())
#define META_DBUS_INPUT_CAPTURE_SKELETON(o)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((o), META_DBUS_TYPE_INPUT_CAPTURE_SKELETON, \
                              MetaDBusInputCaptureSkeleton))
#define META_DBUS_INPUT_CAPTURE_SKELETON_CLASS(k)                      \
  (G_TYPE_CHECK_CLASS_CAST((k), META_DBUS_TYPE_INPUT_CAPTURE_SKELETON, \
                           MetaDBusInputCaptureSkeletonClass))
#define META_DBUS_INPUT_CAPTURE_SKELETON_GET_CLASS(o)                    \
  (G_TYPE_INSTANCE_GET_CLASS((o), META_DBUS_TYPE_INPUT_CAPTURE_SKELETON, \
                             MetaDBusInputCaptureSkeletonClass))
#define META_DBUS_IS_INPUT_CAPTURE_SKELETON(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE((o), META_DBUS_TYPE_INPUT_CAPTURE_SKELETON))
#define META_DBUS_IS_INPUT_CAPTURE_SKELETON_CLASS(k) \
  (G_TYPE_CHECK_CLASS_TYPE((k), META_DBUS_TYPE_INPUT_CAPTURE_SKELETON))

typedef struct _MetaDBusInputCaptureSkeleton MetaDBusInputCaptureSkeleton;
typedef struct _MetaDBusInputCaptureSkeletonClass
    MetaDBusInputCaptureSkeletonClass;
typedef struct _MetaDBusInputCaptureSkeletonPrivate
    MetaDBusInputCaptureSkeletonPrivate;

struct _MetaDBusInputCaptureSkeleton {
  /*< private >*/
  GDBusInterfaceSkeleton parent_instance;
  MetaDBusInputCaptureSkeletonPrivate* priv;
};

struct _MetaDBusInputCaptureSkeletonClass {
  GDBusInterfaceSkeletonClass parent_class;
};

GType meta_dbus_input_capture_skeleton_get_type(void) G_GNUC_CONST;

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MetaDBusInputCaptureSkeleton, g_object_unref)
#endif

MetaDBusInputCapture* meta_dbus_input_capture_skeleton_new(void);

/* ------------------------------------------------------------------------ */
/* Declarations for org.gnome.Mutter.InputCapture.Session */

#define META_DBUS_TYPE_INPUT_CAPTURE_SESSION \
  (meta_dbus_input_capture_session_get_type())
#define META_DBUS_INPUT_CAPTURE_SESSION(o)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((o), META_DBUS_TYPE_INPUT_CAPTURE_SESSION, \
                              MetaDBusInputCaptureSession))
#define META_DBUS_IS_INPUT_CAPTURE_SESSION(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE((o), META_DBUS_TYPE_INPUT_CAPTURE_SESSION))
#define META_DBUS_INPUT_CAPTURE_SESSION_GET_IFACE(o)                        \
  (G_TYPE_INSTANCE_GET_INTERFACE((o), META_DBUS_TYPE_INPUT_CAPTURE_SESSION, \
                                 MetaDBusInputCaptureSessionIface))

struct _MetaDBusInputCaptureSession;
typedef struct _MetaDBusInputCaptureSession MetaDBusInputCaptureSession;
typedef struct _MetaDBusInputCaptureSessionIface
    MetaDBusInputCaptureSessionIface;

struct _MetaDBusInputCaptureSessionIface {
  GTypeInterface parent_iface;

  gboolean (*handle_add_barrier)(MetaDBusInputCaptureSession* object,
                                 GDBusMethodInvocation* invocation,
                                 guint arg_serial,
                                 GVariant* arg_position);

  gboolean (*handle_clear_barriers)(MetaDBusInputCaptureSession* object,
                                    GDBusMethodInvocation* invocation);

  gboolean (*handle_close)(MetaDBusInputCaptureSession* object,
                           GDBusMethodInvocation* invocation);

  gboolean (*handle_connect_to_eis)(MetaDBusInputCaptureSession* object,
                                    GDBusMethodInvocation* invocation,
                                    GUnixFDList* fd_list);

  gboolean (*handle_disable)(MetaDBusInputCaptureSession* object,
                             GDBusMethodInvocation* invocation);

  gboolean (*handle_enable)(MetaDBusInputCaptureSession* object,
                            GDBusMethodInvocation* invocation);

  gboolean (*handle_get_zones)(MetaDBusInputCaptureSession* object,
                               GDBusMethodInvocation* invocation);

  gboolean (*handle_release)(MetaDBusInputCaptureSession* object,
                             GDBusMethodInvocation* invocation,
                             GVariant* arg_options);

  void (*activated)(MetaDBusInputCaptureSession* object,
                    guint arg_barrier_id,
                    guint arg_activation_id,
                    GVariant* arg_cursor_position);

  void (*closed)(MetaDBusInputCaptureSession* object);

  void (*deactivated)(MetaDBusInputCaptureSession* object,
                      guint arg_activation_id);

  void (*disabled)(MetaDBusInputCaptureSession* object);

  void (*zones_changed)(MetaDBusInputCaptureSession* object);
};

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MetaDBusInputCaptureSession, g_object_unref)
#endif

GType meta_dbus_input_capture_session_get_type(void) G_GNUC_CONST;

GDBusInterfaceInfo* meta_dbus_input_capture_session_interface_info(void);
guint meta_dbus_input_capture_session_override_properties(
    GObjectClass* klass,
    guint property_id_begin);

/* D-Bus method call completion functions: */
void meta_dbus_input_capture_session_complete_get_zones(
    MetaDBusInputCaptureSession* object,
    GDBusMethodInvocation* invocation,
    guint serial,
    GVariant* zones);

void meta_dbus_input_capture_session_complete_add_barrier(
    MetaDBusInputCaptureSession* object,
    GDBusMethodInvocation* invocation,
    guint id);

void meta_dbus_input_capture_session_complete_clear_barriers(
    MetaDBusInputCaptureSession* object,
    GDBusMethodInvocation* invocation);

void meta_dbus_input_capture_session_complete_enable(
    MetaDBusInputCaptureSession* object,
    GDBusMethodInvocation* invocation);

void meta_dbus_input_capture_session_complete_disable(
    MetaDBusInputCaptureSession* object,
    GDBusMethodInvocation* invocation);

void meta_dbus_input_capture_session_complete_connect_to_eis(
    MetaDBusInputCaptureSession* object,
    GDBusMethodInvocation* invocation,
    GUnixFDList* fd_list,
    GVariant* fd);

void meta_dbus_input_capture_session_complete_release(
    MetaDBusInputCaptureSession* object,
    GDBusMethodInvocation* invocation);

void meta_dbus_input_capture_session_complete_close(
    MetaDBusInputCaptureSession* object,
    GDBusMethodInvocation* invocation);

/* D-Bus signal emissions functions: */
void meta_dbus_input_capture_session_emit_activated(
    MetaDBusInputCaptureSession* object,
    guint arg_barrier_id,
    guint arg_activation_id,
    GVariant* arg_cursor_position);

void meta_dbus_input_capture_session_emit_deactivated(
    MetaDBusInputCaptureSession* object,
    guint arg_activation_id);

void meta_dbus_input_capture_session_emit_zones_changed(
    MetaDBusInputCaptureSession* object);

void meta_dbus_input_capture_session_emit_disabled(
    MetaDBusInputCaptureSession* object);

void meta_dbus_input_capture_session_emit_closed(
    MetaDBusInputCaptureSession* object);

/* D-Bus method calls: */
void meta_dbus_input_capture_session_call_get_zones(
    MetaDBusInputCaptureSession* proxy,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean meta_dbus_input_capture_session_call_get_zones_finish(
    MetaDBusInputCaptureSession* proxy,
    guint* out_serial,
    GVariant** out_zones,
    GAsyncResult* res,
    GError** error);

gboolean meta_dbus_input_capture_session_call_get_zones_sync(
    MetaDBusInputCaptureSession* proxy,
    guint* out_serial,
    GVariant** out_zones,
    GCancellable* cancellable,
    GError** error);

void meta_dbus_input_capture_session_call_add_barrier(
    MetaDBusInputCaptureSession* proxy,
    guint arg_serial,
    GVariant* arg_position,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean meta_dbus_input_capture_session_call_add_barrier_finish(
    MetaDBusInputCaptureSession* proxy,
    guint* out_id,
    GAsyncResult* res,
    GError** error);

gboolean meta_dbus_input_capture_session_call_add_barrier_sync(
    MetaDBusInputCaptureSession* proxy,
    guint arg_serial,
    GVariant* arg_position,
    guint* out_id,
    GCancellable* cancellable,
    GError** error);

void meta_dbus_input_capture_session_call_clear_barriers(
    MetaDBusInputCaptureSession* proxy,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean meta_dbus_input_capture_session_call_clear_barriers_finish(
    MetaDBusInputCaptureSession* proxy,
    GAsyncResult* res,
    GError** error);

gboolean meta_dbus_input_capture_session_call_clear_barriers_sync(
    MetaDBusInputCaptureSession* proxy,
    GCancellable* cancellable,
    GError** error);

void meta_dbus_input_capture_session_call_enable(
    MetaDBusInputCaptureSession* proxy,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean meta_dbus_input_capture_session_call_enable_finish(
    MetaDBusInputCaptureSession* proxy,
    GAsyncResult* res,
    GError** error);

gboolean meta_dbus_input_capture_session_call_enable_sync(
    MetaDBusInputCaptureSession* proxy,
    GCancellable* cancellable,
    GError** error);

void meta_dbus_input_capture_session_call_disable(
    MetaDBusInputCaptureSession* proxy,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean meta_dbus_input_capture_session_call_disable_finish(
    MetaDBusInputCaptureSession* proxy,
    GAsyncResult* res,
    GError** error);

gboolean meta_dbus_input_capture_session_call_disable_sync(
    MetaDBusInputCaptureSession* proxy,
    GCancellable* cancellable,
    GError** error);

void meta_dbus_input_capture_session_call_connect_to_eis(
    MetaDBusInputCaptureSession* proxy,
    GUnixFDList* fd_list,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean meta_dbus_input_capture_session_call_connect_to_eis_finish(
    MetaDBusInputCaptureSession* proxy,
    GVariant** out_fd,
    GUnixFDList** out_fd_list,
    GAsyncResult* res,
    GError** error);

gboolean meta_dbus_input_capture_session_call_connect_to_eis_sync(
    MetaDBusInputCaptureSession* proxy,
    GUnixFDList* fd_list,
    GVariant** out_fd,
    GUnixFDList** out_fd_list,
    GCancellable* cancellable,
    GError** error);

void meta_dbus_input_capture_session_call_release(
    MetaDBusInputCaptureSession* proxy,
    GVariant* arg_options,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean meta_dbus_input_capture_session_call_release_finish(
    MetaDBusInputCaptureSession* proxy,
    GAsyncResult* res,
    GError** error);

gboolean meta_dbus_input_capture_session_call_release_sync(
    MetaDBusInputCaptureSession* proxy,
    GVariant* arg_options,
    GCancellable* cancellable,
    GError** error);

void meta_dbus_input_capture_session_call_close(
    MetaDBusInputCaptureSession* proxy,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean meta_dbus_input_capture_session_call_close_finish(
    MetaDBusInputCaptureSession* proxy,
    GAsyncResult* res,
    GError** error);

gboolean meta_dbus_input_capture_session_call_close_sync(
    MetaDBusInputCaptureSession* proxy,
    GCancellable* cancellable,
    GError** error);

/* ---- */

#define META_DBUS_TYPE_INPUT_CAPTURE_SESSION_PROXY \
  (meta_dbus_input_capture_session_proxy_get_type())
#define META_DBUS_INPUT_CAPTURE_SESSION_PROXY(o)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((o), META_DBUS_TYPE_INPUT_CAPTURE_SESSION_PROXY, \
                              MetaDBusInputCaptureSessionProxy))
#define META_DBUS_INPUT_CAPTURE_SESSION_PROXY_CLASS(k)                      \
  (G_TYPE_CHECK_CLASS_CAST((k), META_DBUS_TYPE_INPUT_CAPTURE_SESSION_PROXY, \
                           MetaDBusInputCaptureSessionProxyClass))
#define META_DBUS_INPUT_CAPTURE_SESSION_PROXY_GET_CLASS(o)                    \
  (G_TYPE_INSTANCE_GET_CLASS((o), META_DBUS_TYPE_INPUT_CAPTURE_SESSION_PROXY, \
                             MetaDBusInputCaptureSessionProxyClass))
#define META_DBUS_IS_INPUT_CAPTURE_SESSION_PROXY(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE((o), META_DBUS_TYPE_INPUT_CAPTURE_SESSION_PROXY))
#define META_DBUS_IS_INPUT_CAPTURE_SESSION_PROXY_CLASS(k) \
  (G_TYPE_CHECK_CLASS_TYPE((k), META_DBUS_TYPE_INPUT_CAPTURE_SESSION_PROXY))

typedef struct _MetaDBusInputCaptureSessionProxy
    MetaDBusInputCaptureSessionProxy;
typedef struct _MetaDBusInputCaptureSessionProxyClass
    MetaDBusInputCaptureSessionProxyClass;
typedef struct _MetaDBusInputCaptureSessionProxyPrivate
    MetaDBusInputCaptureSessionProxyPrivate;

struct _MetaDBusInputCaptureSessionProxy {
  /*< private >*/
  GDBusProxy parent_instance;
  MetaDBusInputCaptureSessionProxyPrivate* priv;
};

struct _MetaDBusInputCaptureSessionProxyClass {
  GDBusProxyClass parent_class;
};

GType meta_dbus_input_capture_session_proxy_get_type(void) G_GNUC_CONST;

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MetaDBusInputCaptureSessionProxy, g_object_unref)
#endif

void meta_dbus_input_capture_session_proxy_new(GDBusConnection* connection,
                                               GDBusProxyFlags flags,
                                               const gchar* name,
                                               const gchar* object_path,
                                               GCancellable* cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data);
MetaDBusInputCaptureSession* meta_dbus_input_capture_session_proxy_new_finish(
    GAsyncResult* res,
    GError** error);
MetaDBusInputCaptureSession* meta_dbus_input_capture_session_proxy_new_sync(
    GDBusConnection* connection,
    GDBusProxyFlags flags,
    const gchar* name,
    const gchar* object_path,
    GCancellable* cancellable,
    GError** error);

void meta_dbus_input_capture_session_proxy_new_for_bus(
    GBusType bus_type,
    GDBusProxyFlags flags,
    const gchar* name,
    const gchar* object_path,
    GCancellable* cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);
MetaDBusInputCaptureSession*
meta_dbus_input_capture_session_proxy_new_for_bus_finish(GAsyncResult* res,
                                                         GError** error);
MetaDBusInputCaptureSession*
meta_dbus_input_capture_session_proxy_new_for_bus_sync(
    GBusType bus_type,
    GDBusProxyFlags flags,
    const gchar* name,
    const gchar* object_path,
    GCancellable* cancellable,
    GError** error);

/* ---- */

#define META_DBUS_TYPE_INPUT_CAPTURE_SESSION_SKELETON \
  (meta_dbus_input_capture_session_skeleton_get_type())
#define META_DBUS_INPUT_CAPTURE_SESSION_SKELETON(o)                          \
  (G_TYPE_CHECK_INSTANCE_CAST((o),                                           \
                              META_DBUS_TYPE_INPUT_CAPTURE_SESSION_SKELETON, \
                              MetaDBusInputCaptureSessionSkeleton))
#define META_DBUS_INPUT_CAPTURE_SESSION_SKELETON_CLASS(k)                      \
  (G_TYPE_CHECK_CLASS_CAST((k), META_DBUS_TYPE_INPUT_CAPTURE_SESSION_SKELETON, \
                           MetaDBusInputCaptureSessionSkeletonClass))
#define META_DBUS_INPUT_CAPTURE_SESSION_SKELETON_GET_CLASS(o)               \
  (G_TYPE_INSTANCE_GET_CLASS((o),                                           \
                             META_DBUS_TYPE_INPUT_CAPTURE_SESSION_SKELETON, \
                             MetaDBusInputCaptureSessionSkeletonClass))
#define META_DBUS_IS_INPUT_CAPTURE_SESSION_SKELETON(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE((o),                     \
                              META_DBUS_TYPE_INPUT_CAPTURE_SESSION_SKELETON))
#define META_DBUS_IS_INPUT_CAPTURE_SESSION_SKELETON_CLASS(k) \
  (G_TYPE_CHECK_CLASS_TYPE((k), META_DBUS_TYPE_INPUT_CAPTURE_SESSION_SKELETON))

typedef struct _MetaDBusInputCaptureSessionSkeleton
    MetaDBusInputCaptureSessionSkeleton;
typedef struct _MetaDBusInputCaptureSessionSkeletonClass
    MetaDBusInputCaptureSessionSkeletonClass;
typedef struct _MetaDBusInputCaptureSessionSkeletonPrivate
    MetaDBusInputCaptureSessionSkeletonPrivate;

struct _MetaDBusInputCaptureSessionSkeleton {
  /*< private >*/
  GDBusInterfaceSkeleton parent_instance;
  MetaDBusInputCaptureSessionSkeletonPrivate* priv;
};

struct _MetaDBusInputCaptureSessionSkeletonClass {
  GDBusInterfaceSkeletonClass parent_class;
};

GType meta_dbus_input_capture_session_skeleton_get_type(void) G_GNUC_CONST;

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MetaDBusInputCaptureSessionSkeleton,
                              g_object_unref)
#endif

MetaDBusInputCaptureSession* meta_dbus_input_capture_session_skeleton_new(void);

G_END_DECLS

#endif /* __META_DBUS_INPUT_CAPTURE_H__ */
