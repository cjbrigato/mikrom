// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_THEMES_BROWSER_THEME_PACK_H_
#define CHROME_BROWSER_THEMES_BROWSER_THEME_PACK_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/task/sequenced_task_runner_helpers.h"
#include "base/values.h"
#include "chrome/browser/themes/custom_theme_supplier.h"
#include "chrome/common/themes/autogenerated_theme_util.h"
#include "extensions/common/extension.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/resource/resource_scale_factor.h"
#include "ui/gfx/color_utils.h"

namespace base {
class FilePath;
class RefCountedMemory;
}  // namespace base

namespace gfx {
class Image;
}

namespace ui {
class ColorProvider;
class DataPack;
}  // namespace ui

// An optimized representation of a theme, backed by a mmapped DataPack.
//
// The idea is to pre-process all images (tinting, compositing, etc) at theme
// install time, save all the PNG-ified data into an mmappable file so we don't
// suffer multiple file system access times, therefore solving two of the
// problems with the previous implementation.
//
// A note on const-ness. All public, non-static methods are const.  We do this
// because once we've constructed a BrowserThemePack through the
// BuildFromExtension() interface, we WriteToDisk() on a thread other than the
// UI thread that consumes a BrowserThemePack. There is no locking; thread
// safety between the writing thread and the UI thread is ensured by having the
// data be immutable.
//
// BrowserThemePacks are always deleted on a sequence with I/O allowed because
// in the common case, they are backed by mmapped data and the unmmapping
// operation will trip our IO on the UI thread detector.
// See CustomThemeSupplier constructor more more details.
class BrowserThemePack : public CustomThemeSupplier {
 public:
  // Public because these IDs are used both in private members of this class and
  // free functions in browser_theme_pack.cc.
  enum PersistentID : int;

  // Builds the theme from |extension| into |pack|. This may be done on a
  // separate thread as it takes so long. This can fail in the case where the
  // theme has invalid data, in which case |pack->is_valid()| will be false.
  static void BuildFromExtension(const extensions::Extension* extension,
                                 BrowserThemePack* pack);

  // Builds the theme pack from a previously performed WriteToDisk(). This
  // operation should be relatively fast, as it should be an mmap() and some
  // pointer swizzling. Returns NULL on any error attempting to read |path|.
  static scoped_refptr<BrowserThemePack> BuildFromDataPack(
      const base::FilePath& path,
      const std::string& expected_id);

  // Returns whether the specified identifier is one of the images we persist
  // in the data pack.
  static bool IsPersistentImageID(int id);

  // Builds the theme from given |color| into |pack|.
  static void BuildFromColor(SkColor color, BrowserThemePack* pack);

  // Builds the theme from the given |colors| into |pack|.
  static void BuildFromColors(AutogeneratedThemeColors colors,
                              BrowserThemePack* pack);

  // Builds the theme from the given `theme_color` and `background_color` into
  // `pack`, for a Web App browser.
  static void BuildFromWebAppColors(SkColor theme_color,
                                    SkColor background_color,
                                    BrowserThemePack* pack);

  // Default. Everything is empty.
  explicit BrowserThemePack(ThemeType theme_type);

  BrowserThemePack(const BrowserThemePack&) = delete;
  BrowserThemePack& operator=(const BrowserThemePack&) = delete;

  bool is_valid() const { return is_valid_; }

  // Builds a data pack on disk at |path| for future quick loading by
  // BuildFromDataPack(). Often (but not always) called from the file thread;
  // implementation should be threadsafe because neither thread will write to
  // |image_memory_| and the worker thread will keep a reference to prevent
  // destruction.
  bool WriteToDisk(const base::FilePath& path) const;

  // Overridden from CustomThemeSupplier:
  bool GetTint(int id, color_utils::HSL* hsl) const override;
  bool GetColor(int id, SkColor* color) const override;
  bool GetDisplayProperty(int id, int* result) const override;
  gfx::Image GetImageNamed(int id) const override;
  base::RefCountedMemory* GetRawData(
      int id,
      ui::ResourceScaleFactor scale_factor) const override;
  bool HasCustomImage(int id) const override;
  void AddColorMixers(ui::ColorProvider* provider,
                      const ui::ColorProviderKey& key) const override;

 private:
  friend class BrowserThemePackTest;

  // Cached images.
  typedef base::flat_map<PersistentID, gfx::Image> ImageCache;

  // The raw PNG memory associated with a certain id.
  typedef std::map<int, scoped_refptr<base::RefCountedMemory>> RawImages;

  // The type passed to ui::DataPack::WritePack.
  typedef std::map<uint16_t, std::string_view> RawDataForWriting;

  // Maps scale factors (enum values) to file paths.
  typedef base::flat_map<ui::ResourceScaleFactor, base::FilePath>
      ScaleFactorToFileMap;

  // Maps image ids to maps of scale factors to file paths.
  typedef base::flat_map<PersistentID, ScaleFactorToFileMap> FilePathMap;

  ~BrowserThemePack() override;

  // Modifies |colors_| to set the entry with identifier |id| to |color|.  Only
  // valid to call after InitColors(), which creates |colors_|.
  void SetColor(int id, SkColor color);

  // If |colors_| does not already contain an entry with identifier |id|,
  // modifies |colors_| to set the entry with identifier |id| to |color|.  If an
  // entry for |id| already exists, does nothing.
  // Only valid to call after BuildColorsFromJSON(), which creates |colors_|.
  void SetColorIfUnspecified(int id, SkColor color);

  // Sets the value for |id| in |tints_|. Only valid to call after InitTints(),
  // which creates |tints_|.
  void SetTint(int id, color_utils::HSL tint);

  // Sets the value for |id| in |display_properties_|. Only valid to call after
  // InitDisplayProperties(), which creates |display_properties_|.
  void SetDisplayProperty(int id, int value);

  // Calculates the dominant color of the top |height| rows of |image|.
  SkColor ComputeImageColor(const gfx::Image& image, int height);

  // Adjusts/sets theme properties.
  void AdjustThemePack();

  // Initializes necessary fields.
  void InitEmptyPack();

  // Initializes necessary fields for a themepack generated from theme colors.
  void InitEmptyPackFromColors();

  // Initializes the |header_| with default values.
  void InitHeader();

  // Initializes the |tints_| with default values.
  void InitTints();

  // Initializes the |colors_| with default values.
  void InitColors();

  // Initializes the |display_properties_| with default values.
  void InitDisplayProperties();

  // Initializes the |source_images_| with default values.
  void InitSourceImages();

  // Finalizes a theme pack generated from theme colors.
  void FinalizePackFromColors();

  // Sets the ID from |extension|.
  void SetHeaderId(const extensions::Extension* extension);

  // Transforms the JSON tint values into their final versions in the |tints_|
  // array. Does nothing if |tints_value| is nullptr.
  void SetTintsFromJSON(const base::Value::Dict* tints_value);

  // Transforms the JSON color values into their final versions in the
  // |colors_| array and also fills in unspecified colors based on tint values.
  // Does nothing if |color_value| is nullptr.
  void SetColorsFromJSON(const base::Value::Dict* color_value);

  // Implementation details of BuildColorsFromJSON().
  void ReadColorsFromJSON(const base::Value::Dict& colors_value,
                          std::map<int, SkColor>* temp_colors);

  // Transforms the JSON display properties into |display_properties_|. Does
  // nothing if |display_value| is nullptr.
  void SetDisplayPropertiesFromJSON(const base::Value::Dict* display_value);

  // Parses the image names out of an extension. Does nothing if |images_value|
  // is nullptr.
  void ParseImageNamesFromJSON(const base::Value::Dict* images_value,
                               const base::FilePath& images_path,
                               FilePathMap* file_paths) const;

  // Helper function to populate the FilePathMap.
  void AddFileAtScaleToMap(const std::string& image_name,
                           ui::ResourceScaleFactor scale_factor,
                           const base::FilePath& image_path,
                           FilePathMap* file_paths) const;

  // Creates the data for |source_images_| from |file_paths|.
  void BuildSourceImagesArray(const FilePathMap& file_paths);

  // Loads the unmodified images packed in the extension to SkBitmaps. Returns
  // true if all images loaded.
  bool LoadRawBitmapsTo(const FilePathMap& file_paths, ImageCache* image_cache);

  // Crops images down to a size such that most of the cropped image will be
  // displayed in the UI. Cropping is useful because images from custom themes
  // can be of any size. Source and destination is |images|.
  void CropImages(ImageCache* images) const;

  // Set frame and toolbar related elements' colors (e.g. status bubble,
  // info bar, download shelf) to frame or toolbar colors.
  void SetFrameAndToolbarRelatedColors();

  // Creates a composited toolbar image. Source and destination is |images|.
  // Also sets toolbar color corresponding to this image.
  void CreateToolbarImageAndColors(ImageCache* images);

  // Creates tinted and composited frame images. Source and destination is
  // |images|.  Also sets frame colors corresponding to these images if no
  // explicit color has been specified for these colors.
  void CreateFrameImagesAndColors(ImageCache* images);

  // Generates any frame colors which have not already been set from tints.
  void GenerateFrameColorsFromTints();

  // Generates background color information for the background of window control
  // buttons.  This can be used when drawing the window control/caption buttons
  // to determine what color to draw the symbol, ensuring that it contrasts
  // sufficiently with the background of the button.
  void GenerateWindowControlButtonColor(ImageCache* images);

  // Creates the semi-transparent tab background images, putting the results
  // in |images|.  Also sets colors corresponding to these images if no explicit
  // color has been specified.  Must be called after GenerateFrameImages().
  void CreateTabBackgroundImagesAndColors(ImageCache* images);

  // Generates missing NTP related colors.
  void GenerateMissingNtpColors();

  // Takes all the SkBitmaps in |images|, encodes them as PNGs and places
  // them in |reencoded_images|.
  void RepackImages(const ImageCache& images,
                    RawImages* reencoded_images) const;

  // Takes all images in |source| and puts them in |destination|, freeing any
  // image already in |destination| that |source| would overwrite.
  void MergeImageCaches(const ImageCache& source,
                        ImageCache* destination) const;

  // Copies images from |source| to |destination| such that the lifetimes of
  // the images in |destination| are not affected by the lifetimes of the
  // images in |source|.
  void CopyImagesTo(const ImageCache& source, ImageCache* destination) const;

  // Changes the RefCountedMemory based |images| into string_view data in
  // |out|.
  void AddRawImagesTo(const RawImages& images, RawDataForWriting* out) const;

  // Retrieves the tint OR the default tint. Unlike the public interface, we
  // always need to return a reasonable tint here, instead of partially
  // querying if the tint exists.
  color_utils::HSL GetTintInternal(int id) const;

  // Returns a unique id to use to store the raw bitmap for |prs_id| at
  // |scale_factor| in memory.
  int GetRawIDByPersistentID(PersistentID prs_id,
                             ui::ResourceScaleFactor scale_factor) const;

  // Returns true if the |key| specifies a valid scale (e.g. "100") and
  // the corresponding scale factor is currently in use. If true, returns
  // the scale factor in |scale_factor|.
  bool GetScaleFactorFromManifestKey(
      const std::string& key,
      ui::ResourceScaleFactor* scale_factor) const;

  // Generates raw images for any missing scale from an available scale.
  void GenerateRawImageForAllSupportedScales(PersistentID prs_id);

  // Data pack, if we have one.
  std::unique_ptr<ui::DataPack> data_pack_;
  bool using_data_pack_ = false;

  // All structs written to disk need to be packed; no alignment tricks here,
  // please.
  // NOTE: This structs can only contain primary data types to be reliably
  // seralized and de-seralized. Not even nested structs will work across
  // different machines, see crbug.com/988055.
#pragma pack(push, 1)
  // Header that is written to disk.
  struct BrowserThemePackHeader {
    // Numeric version to make sure we're compatible in the future.
    int32_t version;

    // 1 if little_endian. 0 if big_endian. On mismatch, abort load.
    int32_t little_endian;

    // theme_id without NULL terminator.
    uint8_t theme_id[16];
  };
  raw_ptr<BrowserThemePackHeader, DanglingUntriaged> header_ = nullptr;

  // The remaining structs represent individual entries in an array. For the
  // following three structs, BrowserThemePack will either allocate an array or
  // will point directly to mmapped data.
  struct TintEntry {
    int32_t id;
    double h;
    double s;
    double l;
  };
  raw_ptr<TintEntry, DanglingUntriaged | AllowPtrArithmetic> tints_ = nullptr;

  struct ColorPair {
    int32_t id;
    SkColor color;
  };
  raw_ptr<ColorPair, DanglingUntriaged | AllowPtrArithmetic> colors_ = nullptr;

  struct DisplayPropertyPair {
    int32_t id;
    int32_t property;
  };
  raw_ptr<DisplayPropertyPair, DanglingUntriaged | AllowPtrArithmetic>
      display_properties_ = nullptr;

  // A list of included source images. A pointer to a -1 terminated array of
  // our persistent IDs. The IDs are `int`s, but must be wrapped in a struct so
  // that `#pragma pack` above applies.
  struct SourceImage {
    int id;
  };
  raw_ptr<SourceImage, DanglingUntriaged | AllowPtrArithmetic> source_images_ =
      nullptr;
#pragma pack(pop)

  // The scale factors represented by the images in the theme pack.
  std::vector<ui::ResourceScaleFactor> scale_factors_;

  // References to raw PNG data. This map isn't touched when |data_pack_| is
  // non-NULL; |image_memory_| is only filled during BuildFromExtension(). Any
  // image data that needs to be written to the DataPack during WriteToDisk()
  // needs to be in |image_memory_|.
  RawImages image_memory_;

  // Cached loaded images. These are loaded from |image_memory_|, from
  // |data_pack_|, and by BuildFromExtension().
  mutable ImageCache images_;

  // Cache of images created in BuildFromExtension(). Once the theme pack is
  // created, this cache should only be accessed on the file thread. There
  // should be no IDs in |image_memory_| that are in |images_on_file_thread_|
  // or vice versa.
  ImageCache images_on_file_thread_;

  // Whether the theme pack has been succesfully initialized and is ready to
  // use.
  bool is_valid_ = false;
};

#endif  // CHROME_BROWSER_THEMES_BROWSER_THEME_PACK_H_
