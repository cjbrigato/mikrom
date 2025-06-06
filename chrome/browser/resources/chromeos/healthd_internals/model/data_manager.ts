// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert} from '//resources/js/assert.js';
import {sendWithPromise} from '//resources/js/cr.js';

import type {SystemTrendController} from '../controller/system_trend_controller.js';
import type {CrosSystemResult, HealthdApiBatteryResult, HealthdApiCpuResult, HealthdApiMemoryResult, HealthdApiTelemetryResult, HealthdApiThermalResult, SystemZramInfo} from '../utils/externs.js';
import type {HealthdInternalsInfoElement} from '../view/pages/info.js';
import type {HealthdInternalsTelemetryElement} from '../view/pages/telemetry.js';

import {CpuUsageHelper} from './cpu_usage_helper.js';
import type {CpuUsage} from './cpu_usage_helper.js';
import {DataSeries} from './data_series.js';

const LINE_CHART_BATTERY_HEADERS: string[] = [
  'Voltage (V)',
  'Charge (Ah)',
  'Current (A)',
];

const LINE_CHART_MEMORY_HEADERS: string[] = [
  'Available',
  'Free',
  'Buffers',
  'Page Cache',
  'Shared',
  'Active',
  'Inactive',
  'Total Slab',
  'Reclaim Slab',
  'Unreclaim Slab',
];

const LINE_CHART_ZRAM_HEADERS: string[] = [
  'Total Used',
  'Original Size',
  'Compressed Size',
];


function sortThermals(
    first: HealthdApiThermalResult, second: HealthdApiThermalResult): number {
  if (first.source === second.source) {
    return first.name.localeCompare(second.name);
  }
  return first.source.localeCompare(second.source);
}

/**
 * Helper class to collect and maintain the displayed data.
 */
export class DataManager {
  constructor(
      dataRetentionDuration: number, infoPage: HealthdInternalsInfoElement,
      telemetryPage: HealthdInternalsTelemetryElement,
      systemTrendController: SystemTrendController) {
    this.dataRetentionDuration = dataRetentionDuration;

    this.infoPage = infoPage;
    this.telemetryPage = telemetryPage;
    this.systemTrendController = systemTrendController;

    this.initBatteryDataSeries();
    this.initMemoryDataSeries();
    this.initZramDataSeries();
  }

  // Historical data for line chart. The following `DataSeries` collection
  // is fixed and initialized in constructor.
  // - Battery data.
  private batteryDataSeries: DataSeries[] = [];
  // - Memory data.
  private memoryDataSeries: DataSeries[] = [];
  // - Zram data.
  private zramDataSeries: DataSeries[] = [];

  // Historical data for line chart. The following `DataSeries` collection
  // is dynamic and initialized when the first batch of data is obtained.
  // - Frequency data of logical CPUs.
  private cpuFrequencyDataSeries: DataSeries[] = [];
  // - CPU usage of logical CPUs in percentage.
  private cpuUsageDataSeries: DataSeries[] = [];
  // - Temperature data of thermal sensors.
  private thermalDataSeries: DataSeries[] = [];

  // Set in constructor.
  private readonly infoPage: HealthdInternalsInfoElement;
  private readonly telemetryPage: HealthdInternalsTelemetryElement;
  private readonly systemTrendController: SystemTrendController;

  // The helper class for calculating CPU usage.
  private readonly cpuUsageHelper: CpuUsageHelper = new CpuUsageHelper();

  // The data fetching interval IDs used for cancelling the running interval.
  private fetchDataInternalIds: number[] = [];

  // The duration (in milliseconds) that the data will be retained.
  private dataRetentionDuration: number;

  /**
   * Set up periodic fetch data requests to the backend to get required info.
   *
   * @param pollingCycle - Polling cycle in milliseconds.
   */
  setupFetchDataRequests(pollingCycle: number) {
    if (this.fetchDataInternalIds.length !== 0) {
      for (const internalId of this.fetchDataInternalIds) {
        clearInterval(internalId);
      }
      this.fetchDataInternalIds = [];
    }

    const fetchHealthdData = () => {
      sendWithPromise('getHealthdTelemetryInfo')
          .then((data: HealthdApiTelemetryResult) => {
            this.handleHealthdTelemetryInfo(data);
          });
    };
    fetchHealthdData();
    this.fetchDataInternalIds.push(setInterval(fetchHealthdData, pollingCycle));

    // TODO(crbug.com/362430588): `getCrosSystemInfo` is a workaround API for
    // collecting data from SysFs or libchrome. The data should be collected in
    // `cros_healthd` and requested in the `getHealthdTelemetryInfo` API.
    const fetchSystemData = () => {
      sendWithPromise('getCrosSystemInfo').then((data: CrosSystemResult) => {
        this.handleSystemZramInfo(data);
      });
    };
    fetchSystemData();
    this.fetchDataInternalIds.push(setInterval(fetchSystemData, pollingCycle));
  }

  updateDataRetentionDuration(durationHours: number) {
    this.dataRetentionDuration = durationHours * 60 * 60 * 1000;
    this.removeOutdatedData(Date.now());
  }

  private handleHealthdTelemetryInfo(data: HealthdApiTelemetryResult) {
    data.thermals.sort(sortThermals);

    this.infoPage.updateTelemetryData(data);
    this.telemetryPage.updateTelemetryData(data);

    const timestamp: number = Date.now();
    if (data.battery !== undefined) {
      this.updateBatteryData(data.battery, timestamp);
    }
    this.updateCpuFrequencyData(data.cpu, timestamp);
    this.updateMemoryData(data.memory, timestamp);
    this.updateThermalData(data.thermals, timestamp);

    const cpuUsage = this.cpuUsageHelper.getCpuUsage(data.cpu);
    if (cpuUsage !== null) {
      this.updateCpuUsageData(cpuUsage, timestamp);
      this.infoPage.updateCpuUsageData(cpuUsage);
      this.telemetryPage.updateCpuUsageData(cpuUsage);
    }

    this.removeOutdatedData(timestamp);
  }

  private handleSystemZramInfo(data: CrosSystemResult) {
    const timestamp: number = Date.now();
    if (data.zram !== undefined) {
      this.updateZramData(data.zram, timestamp);
      this.infoPage.updateZramData(data.zram);
      this.telemetryPage.updateZramData(data.zram);
    }
    this.removeOutdatedData(timestamp);
  }

  private removeOutdatedData(endTime: number) {
    const newStartTime = endTime - this.dataRetentionDuration;
    for (const dataSeriesList
             of [this.batteryDataSeries, this.cpuFrequencyDataSeries,
                 this.cpuUsageDataSeries, this.memoryDataSeries,
                 this.thermalDataSeries, this.zramDataSeries]) {
      for (const dataSeries of dataSeriesList) {
        dataSeries.removeOutdatedData(newStartTime);
      }
    }
  }

  private updateBatteryData(
      battery: HealthdApiBatteryResult, timestamp: number) {
    this.batteryDataSeries[0].addDataPoint(battery.voltageNow, timestamp);
    this.batteryDataSeries[1].addDataPoint(battery.chargeNow, timestamp);
    this.batteryDataSeries[2].addDataPoint(battery.currentNow, timestamp);
  }

  private updateCpuFrequencyData(cpu: HealthdApiCpuResult, timestamp: number) {
    if (this.cpuFrequencyDataSeries.length === 0) {
      this.initCpuFrequencyDataSeries(cpu);
    }

    const cpuNumber: number = cpu.physicalCpus.reduce(
        (acc, item) => acc + item.logicalCpus.length, 0);
    if (cpuNumber !== this.cpuFrequencyDataSeries.length) {
      console.warn('CPU frequency data: Number of CPUs changed.');
      return;
    }

    let count: number = 0;
    for (const physicalCpu of cpu.physicalCpus) {
      for (const logicalCpu of physicalCpu.logicalCpus) {
        this.cpuFrequencyDataSeries[count].addDataPoint(
            parseInt(logicalCpu.frequency.current), timestamp);
        count += 1;
      }
    }
  }

  private updateCpuUsageData(
      physcialCpuUsage: Array<Array<CpuUsage|null>>, timestamp: number) {
    if (this.cpuUsageDataSeries.length === 0) {
      this.initCpuUsageDataSeries(physcialCpuUsage);
    }

    const cpuNumber: number =
        physcialCpuUsage.reduce((acc, item) => acc + item.length, 0);
    if (cpuNumber !== this.cpuUsageDataSeries.length - 1) {
      console.warn('CPU usage data: Number of CPUs changed.');
      return;
    }

    if (cpuNumber === 0) {
      console.warn('CPU usage data: CPU not found.');
      return;
    }

    let sumCpuUsage: number = 0;
    let count: number = 1;
    for (const logicalCpuUsage of physcialCpuUsage) {
      for (const cpuUsage of logicalCpuUsage) {
        if (cpuUsage !== null) {
          const usage = cpuUsage.userPercentage + cpuUsage.systemPercentage;
          this.cpuUsageDataSeries[count].addDataPoint(usage, timestamp);
          sumCpuUsage += usage;
        }
        count += 1;
      }
    }
    this.cpuUsageDataSeries[0].addDataPoint(sumCpuUsage / cpuNumber, timestamp);
  }

  private updateMemoryData(memory: HealthdApiMemoryResult, timestamp: number) {
    const itemsInChart: Array<string|undefined> = [
      memory.availableMemoryKib,
      memory.freeMemoryKib,
      memory.buffersKib,
      memory.pageCacheKib,
      memory.sharedMemoryKib,
      memory.activeMemoryKib,
      memory.inactiveMemoryKib,
      memory.totalSlabMemoryKib,
      memory.reclaimableSlabMemoryKib,
      memory.unreclaimableSlabMemoryKib,
    ];
    assert(itemsInChart.length === this.memoryDataSeries.length);
    for (const [index, item] of itemsInChart.entries()) {
      if (item !== undefined) {
        this.memoryDataSeries[index].addDataPoint(parseInt(item), timestamp);
      }
    }
  }

  private updateThermalData(
      thermals: HealthdApiThermalResult[], timestamp: number) {
    if (this.thermalDataSeries.length === 0) {
      this.initThermalDataSeries(thermals);
    }

    if (thermals.length !== this.thermalDataSeries.length) {
      console.warn('Thermal data: Number of thermal sensors changed.');
      return;
    }

    for (const [index, thermal] of thermals.entries()) {
      this.thermalDataSeries[index].addDataPoint(
          thermal.temperatureCelsius, timestamp);
    }
  }

  private updateZramData(zram: SystemZramInfo, timestamp: number) {
    const zramDataPoints: string[] =
        [zram.totalUsedMemory, zram.originalDataSize, zram.compressedDataSize];
    for (const [index, zramData] of zramDataPoints.entries()) {
      this.zramDataSeries[index].addDataPoint(parseInt(zramData), timestamp);
    }
  }

  private initBatteryDataSeries() {
    for (const header of LINE_CHART_BATTERY_HEADERS) {
      this.batteryDataSeries.push(new DataSeries(header, 'Battery'));
    }
    this.systemTrendController.setBatteryData(this.batteryDataSeries);
  }

  private initCpuFrequencyDataSeries(cpu: HealthdApiCpuResult) {
    for (const [physicalCpuId, physicalCpu] of cpu.physicalCpus.entries()) {
      for (let logicalCpuId: number = 0;
           logicalCpuId < physicalCpu.logicalCpus.length; ++logicalCpuId) {
        this.cpuFrequencyDataSeries.push(
            new DataSeries(`CPU #${physicalCpuId}-${logicalCpuId}`, 'Freq'));
      }
    }
    this.systemTrendController.setCpuFrequencyData(this.cpuFrequencyDataSeries);
  }

  private initCpuUsageDataSeries(physcialCpuUsage:
                                     Array<Array<CpuUsage|null>>) {
    this.cpuUsageDataSeries.push(new DataSeries('Overall', 'Usage'));
    for (const [physicalCpuId, logicalCpuUsage] of physcialCpuUsage.entries()) {
      for (let logicalCpuId: number = 0; logicalCpuId < logicalCpuUsage.length;
           ++logicalCpuId) {
        this.cpuUsageDataSeries.push(
            new DataSeries(`CPU #${physicalCpuId}-${logicalCpuId}`, 'Usage'));
      }
    }
    this.systemTrendController.setCpuUsageData(this.cpuUsageDataSeries);
  }

  private initMemoryDataSeries() {
    for (const header of LINE_CHART_MEMORY_HEADERS) {
      this.memoryDataSeries.push(new DataSeries(header, 'Memory'));
    }
    this.systemTrendController.setMemoryData(this.memoryDataSeries);
  }

  private initThermalDataSeries(thermals: HealthdApiThermalResult[]) {
    for (const thermal of thermals) {
      this.thermalDataSeries.push(
          new DataSeries(`${thermal.name} (${thermal.source})`, 'Thermal'));
    }
    this.systemTrendController.setThermalData(this.thermalDataSeries);
  }

  private initZramDataSeries() {
    for (const header of LINE_CHART_ZRAM_HEADERS) {
      this.zramDataSeries.push(new DataSeries(header, 'Zram'));
    }
    this.systemTrendController.setZramData(this.zramDataSeries);
  }
}
