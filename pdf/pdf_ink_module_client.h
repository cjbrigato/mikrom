// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PDF_PDF_INK_MODULE_CLIENT_H_
#define PDF_PDF_INK_MODULE_CLIENT_H_

#include <map>

#include "pdf/buildflags.h"
#include "pdf/page_orientation.h"
#include "pdf/pdf_ink_ids.h"
#include "pdf/ui/thumbnail.h"
#include "third_party/ink/src/ink/geometry/partitioned_mesh.h"
#include "ui/base/cursor/cursor.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"

static_assert(BUILDFLAG(ENABLE_PDF_INK2), "ENABLE_PDF_INK2 not set to true");

namespace gfx {
class PointF;
}

namespace ink {
class Stroke;
}

namespace ui {
class Cursor;
}

namespace chrome_pdf {

class PdfInkModuleClient {
 public:
  // Key: ID to identify a shape.
  // Value: The Ink shape.
  using PageV2InkPathShapesMap =
      std::map<InkModeledShapeId, ink::PartitionedMesh>;

  // Key: 0-based page index.
  // Value: Map of shapes on the page.
  using DocumentV2InkPathShapesMap = std::map<int, PageV2InkPathShapesMap>;

  virtual ~PdfInkModuleClient() = default;

  // Notifies the client to clear the current text selection.
  virtual void ClearSelection() {}

  // Asks the client to discard the stroke identified by `id` on the page at
  // `page_index`.
  virtual void DiscardStroke(int page_index, InkStrokeId id) {}

  // Extends the current text selection to the nearest page and character to
  // `point`. `point` must be in device coordinates.
  virtual void ExtendSelectionByPoint(const gfx::PointF& point) {}

  // Returns the current cursor.
  virtual ui::Cursor GetCursor() = 0;

  // Gets the current page orientation.
  virtual PageOrientation GetOrientation() const = 0;

  // Gets the current scaled and rotated rectangle area of the page in CSS
  // screen coordinates for the 0-based page index.  Must be non-empty for any
  // non-negative page index returned from `VisiblePageIndexFromPoint()`.
  virtual gfx::Rect GetPageContentsRect(int page_index) = 0;

  // Gets the page size in points for `page_index`.  Must be non-empty for any
  // non-negative page index returned from `VisiblePageIndexFromPoint()`.
  virtual gfx::SizeF GetPageSizeInPoints(int page_index) = 0;

  // Returns all current text selection rects in device coordinates.
  virtual std::vector<gfx::Rect> GetSelectionRects() = 0;

  // Gets the thumbnail size for `page_index`. The size must be non-empty for
  // any valid page index.
  virtual gfx::Size GetThumbnailSize(int page_index) = 0;

  // Gets the offset within the rendering viewport to where the page images
  // will be drawn.  Since the offset is a location within the viewport, it
  // must always contain non-negative values.  Values are in scaled CSS
  // screen coordinates, where the amount of scaling matches that of
  // `GetZoom()`.  The page orientation does not apply to the viewport.
  virtual gfx::Vector2dF GetViewportOriginOffset() = 0;

  // Gets current zoom factor.
  virtual float GetZoom() const = 0;

  // Notifies the client to invalidate the `rect`.  Coordinates are
  // screen-based, based on the same viewport origin that was used to specify
  // the `blink::WebMouseEvent` positions during stroking.
  virtual void Invalidate(const gfx::Rect& rect) {}

  // Returns whether the page at `page_index` is visible or not.
  virtual bool IsPageVisible(int page_index) = 0;

  // Returns whether `point` is within a selectable text area or a link area.
  // `point` must be in device coordinates.
  virtual bool IsSelectableTextOrLinkArea(const gfx::PointF& point) = 0;

  // Asks the client to load Ink data from the PDF.
  virtual DocumentV2InkPathShapesMap LoadV2InkPathsFromPdf() = 0;

  // Notifies the client whether annotation mode is enabled or not.
  virtual void OnAnnotationModeToggled(bool enable) {}

  // Notifies the client that a text area was clicked at `point` `click_count`
  // times during Ink annotation mode. It is the client's responsibility to
  // handle text selection, copying behavior from Blink. Two click counts should
  // select the word at `point`, while three click counts should select the
  // entire line at `point`. `point` must be in device coordinates.
  virtual void OnTextOrLinkAreaClick(const gfx::PointF& point,
                                     int click_count) {}

  // Returns the 0-based page index for the given `point`. `point` must be on a
  // page, otherwise returns -1. `point` must be in device coordinates.
  virtual int PageIndexFromPoint(const gfx::PointF& point) = 0;

  // Asks the client to post `message`.
  virtual void PostMessage(base::Value::Dict message) {}

  // Asks the client to update the page thumbnail for `page_index`. Note that
  // this is the regular page thumbnail, and not the thumbnail with the Ink
  // strokes.
  virtual void RequestThumbnail(int page_index,
                                SendThumbnailCallback callback) {}

  // Notifies that a stroke has been added to the page at `page_index`.
  // Provides an `id` that identifies the `stroke` object.  The `id` can be
  // used later with `UpdateStrokeActive()`.
  virtual void StrokeAdded(int page_index,
                           InkStrokeId id,
                           const ink::Stroke& stroke) {}

  // Notifies the client that stroking has finished. `modified` indicates
  // whether strokes got added or erased.
  virtual void StrokeFinished(bool modified) {}

  // Notifies the client that a stroke has started drawing or erasing.
  virtual void StrokeStarted() {}

  // Asks the client to change the cursor to `cursor`.
  virtual void UpdateInkCursor(const ui::Cursor& cursor) {}

  // Notifies that an existing shape identified by `id` on the page at
  // `page_index` should update its active state.
  virtual void UpdateShapeActive(int page_index,
                                 InkModeledShapeId id,
                                 bool active) {}

  // Notifies that an existing stroke identified by `id` on the page at
  // `page_index` should update its active state.
  virtual void UpdateStrokeActive(int page_index, InkStrokeId id, bool active) {
  }

  // Same as `PageIndexFromPoint()`, but `point` must be on a visible page,
  // otherwise returns -1.
  virtual int VisiblePageIndexFromPoint(const gfx::PointF& point) = 0;
};

}  // namespace chrome_pdf

#endif  // PDF_PDF_INK_MODULE_CLIENT_H_
