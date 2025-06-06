// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

interface DomAutomationController {
  send(message: any): void;
}

interface Window {
  domAutomationController: DomAutomationController;
}
