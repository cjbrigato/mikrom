// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_PRINTING_PRINTING_API_HANDLER_H_
#define CHROME_BROWSER_EXTENSIONS_API_PRINTING_PRINTING_API_HANDLER_H_

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/types/expected.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/extensions/api/printing/print_job_submitter.h"
#include "chrome/common/extensions/api/printing.h"
#include "chromeos/crosapi/mojom/local_printer.mojom.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"
#include "extensions/browser/event_router_factory.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/gfx/native_widget_types.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/ash/printing/cups_print_job_manager_factory.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

class PrefRegistrySimple;

namespace chromeos {
class CupsWrapper;
class Printer;
}  // namespace chromeos

namespace content {
class BrowserContext;
}  // namespace content

namespace printing {
class PdfBlobDataFlattener;
class PrintJobController;
struct PrinterStatus;
struct PrintJobCreatedInfo;
}  // namespace printing

namespace extensions {

class ExtensionRegistry;

// Handles chrome.printing API functions calls, acts as a PrintJobObserver,
// and generates OnJobStatusChanged() events of chrome.printing API.
// The callback function is never run directly - it is posted to
// base::SequencedTaskRunner::GetCurrentDefault().
class PrintingAPIHandler : public BrowserContextKeyedAPI,
                           public crosapi::mojom::PrintJobObserver {
 public:
  using SubmitJobCallback = base::OnceCallback<void(
      std::optional<api::printing::SubmitJobStatus> status,
      std::optional<std::string> job_id,
      std::optional<std::string> error)>;
  using GetPrintersCallback =
      base::OnceCallback<void(std::vector<api::printing::Printer>)>;
  using GetPrinterInfoCallback = base::OnceCallback<void(
      std::optional<base::Value> capabilities,
      std::optional<api::printing::PrinterStatus> status,
      std::optional<std::string> error)>;

  static std::unique_ptr<PrintingAPIHandler> CreateForTesting(
      content::BrowserContext* browser_context,
      EventRouter* event_router,
      ExtensionRegistry* extension_registry,
      std::unique_ptr<printing::PrintJobController> print_job_controller,
      std::unique_ptr<chromeos::CupsWrapper> cups_wrapper,
      crosapi::mojom::LocalPrinter* local_printer);

  explicit PrintingAPIHandler(content::BrowserContext* browser_context);

  PrintingAPIHandler(
      content::BrowserContext* browser_context,
      EventRouter* event_router,
      ExtensionRegistry* extension_registry,
      std::unique_ptr<printing::PrintJobController> print_job_controller,
      std::unique_ptr<chromeos::CupsWrapper> cups_wrapper,
      crosapi::mojom::LocalPrinter* local_printer);

  PrintingAPIHandler(const PrintingAPIHandler&) = delete;
  PrintingAPIHandler& operator=(const PrintingAPIHandler&) = delete;
  ~PrintingAPIHandler() override;

  static std::string CreateUniqueId(const std::string& printer_id, int job_id);

  // BrowserContextKeyedAPI:
  static BrowserContextKeyedAPIFactory<PrintingAPIHandler>*
  GetFactoryInstance();

  // Returns the current instance for `browser_context`.
  static PrintingAPIHandler* Get(content::BrowserContext* browser_context);

  // crosapi::mojom::PrintJobObserver:
  void OnPrintJobUpdateDeprecated(
      const std::string& printer_id,
      unsigned int job_id,
      crosapi::mojom::PrintJobStatus status) override;
  void OnPrintJobUpdate(const std::string& printer_id,
                        unsigned int job_id,
                        crosapi::mojom::PrintJobUpdatePtr update) override;

  // Register the printing API preference with the `registry`.
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // Submits the job to printing pipeline.
  // If `extension` is not present among PrintingAPIExtensionsAllowlist
  // extensions, special print job request dialog is shown to the user to ask
  // for their confirmation.
  // `native_window` is needed to show this dialog.
  void SubmitJob(gfx::NativeWindow native_window,
                 scoped_refptr<const extensions::Extension> extension,
                 std::optional<api::printing::SubmitJob::Params> params,
                 SubmitJobCallback callback);

  // Returns an error message if an error occurred.
  std::optional<std::string> CancelJob(const std::string& extension_id,
                                       const std::string& job_id);

  void GetPrinters(GetPrintersCallback callback);

  void GetPrinterInfo(const std::string& printer_id,
                      GetPrinterInfoCallback callback);

  base::expected<api::printing::JobStatus, std::string> GetJobStatus(
      const std::string& extension_id,
      const std::string& job_id);

 private:
  // Needed for BrowserContextKeyedAPI implementation.
  friend class BrowserContextKeyedAPIFactory<PrintingAPIHandler>;

  FRIEND_TEST_ALL_PREFIXES(PrintingAPIHandlerParam, GetJobStatus_CacheFull);
  FRIEND_TEST_ALL_PREFIXES(PrintingAPIHandlerUnittest, EvictOldFinishedJobs);

  struct PrintJobInfo {
    std::string printer_id;
    int job_id;
    std::string extension_id;
    api::printing::JobStatus status;
  };

  void OnPrintJobSubmitted(SubmitJobCallback callback,
                           const std::string& extension_id,
                           PrintJobSubmitter::PrintJobCreationResult result);

  void OnPrintersRetrieved(
      GetPrintersCallback callback,
      std::vector<crosapi::mojom::LocalDestinationInfoPtr> data);

  // GetPrinterInfo() calls this function.
  void OnPrinterCapabilitiesRetrieved(
      const std::string& printer_id,
      GetPrinterInfoCallback callback,
      crosapi::mojom::CapabilitiesResponsePtr caps);

  // OnPrinterCapabilitiesRetrieved() calls this function.
  void OnPrinterStatusRetrieved(
      GetPrinterInfoCallback callback,
      base::Value capabilities,
      std::unique_ptr<::printing::PrinterStatus> printer_status);

  // Evicts some jobs from `finished_print_jobs_` to keep them within a certain
  // threshold.
  void MaybeEvictFinishedPrintJobs();

  // BrowserContextKeyedAPI:
  static const bool kServiceIsNULLWhileTesting = true;
  static const bool kServiceIsCreatedWithBrowserContext = false;
  static const char* service_name() { return "PrintingAPIHandler"; }

  const raw_ptr<content::BrowserContext> browser_context_;
  const raw_ptr<EventRouter> event_router_;
  const raw_ptr<ExtensionRegistry> extension_registry_;

  std::unique_ptr<printing::PrintJobController> print_job_controller_;
  std::unique_ptr<chromeos::CupsWrapper> cups_wrapper_;

  const std::unique_ptr<printing::PdfBlobDataFlattener>
      pdf_blob_data_flattener_;

  // Stores mapping from job id to PrintJobInfo object.
  // This is needed to cancel print jobs and keep track of the work in progress
  // jobs.
  base::flat_map<std::string, PrintJobInfo> in_progress_print_jobs_;

  // Stores mapping from jobs' id to their status and the extension id that
  // triggered this job. Stores only finished jobs' id, which can either be
  // printed, cancelled, or failed.
  std::unordered_map<
      /*job_id=*/std::string,
      std::pair</*extension_id=*/std::string, api::printing::JobStatus>>
      finished_print_jobs_;
  // Stores the order of finished jobs' id in order to efficiently remove the
  // oldest jobs from `finished_print_jobs_`.
  base::circular_deque<std::string> finished_jobs_order_;

  raw_ptr<crosapi::mojom::LocalPrinter> local_printer_;

  mojo::Receiver<crosapi::mojom::PrintJobObserver> receiver_{this};

  base::WeakPtrFactory<PrintingAPIHandler> weak_ptr_factory_{this};
};

template <>
struct BrowserContextFactoryDependencies<PrintingAPIHandler> {
  static void DeclareFactoryDependencies(
      BrowserContextKeyedAPIFactory<PrintingAPIHandler>* factory) {
    factory->DependsOn(EventRouterFactory::GetInstance());
#if BUILDFLAG(IS_CHROMEOS)
    factory->DependsOn(ash::CupsPrintJobManagerFactory::GetInstance());
#endif  // BUILDFLAG(IS_CHROMEOS)
  }
};

template <>
std::unique_ptr<KeyedService>
BrowserContextKeyedAPIFactory<PrintingAPIHandler>::
    BuildServiceInstanceForBrowserContext(
        content::BrowserContext* context) const;

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_PRINTING_PRINTING_API_HANDLER_H_
