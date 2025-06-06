// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {BackgroundEl, getGlobalConfig as getBackgroundElGlobalConfig, setGlobalConfig as setBackgroundElGlobalConfig} from './background_el.js';
import {Cloud} from './cloud.js';
import {HorizonLine} from './horizon_line.js';
import {NightMode} from './night_mode.js';
import {Obstacle, setMaxGapCoefficient as setMaxObstacleGapCoefficient, setMaxObstacleLength} from './obstacle.js';
import {Runner} from './offline.js';
import {ObstacleType, spriteDefinitionByType} from './offline_sprite_definitions.js';
import {getRandomNum} from './utils.js';

/**
 * @type{ObstacleType[]}
 */
let obstacleTypes = [];

/**
 * Horizon background class.
 */
export class Horizon {
  /**
   * @param {HTMLCanvasElement} canvas
   * @param {Object} spritePos Sprite positioning.
   * @param {Object} dimensions Canvas dimensions.
   * @param {number} gapCoefficient
   */
  constructor(canvas, spritePos, dimensions, gapCoefficient) {
    this.canvas = canvas;
    this.canvasCtx =
        /** @type {CanvasRenderingContext2D} */ (this.canvas.getContext('2d'));
    this.config = Horizon.config;
    this.dimensions = dimensions;
    this.gapCoefficient = gapCoefficient;
    this.obstacles = [];
    this.obstacleHistory = [];
    this.horizonOffsets = [0, 0];
    this.cloudFrequency = this.config.CLOUD_FREQUENCY;
    this.spritePos = spritePos;
    this.nightMode = null;
    this.altGameModeActive = false;

    // Cloud
    this.clouds = [];
    this.cloudSpeed = this.config.BG_CLOUD_SPEED;

    // Background elements
    this.backgroundEls = [];
    this.lastEl = null;
    this.backgroundSpeed = this.config.BG_CLOUD_SPEED;

    // Horizon
    this.horizonLine = null;
    this.horizonLines = [];
    this.init();
  }



  /**
   * Initialise the horizon. Just add the line and a cloud. No obstacles.
   */
  init() {
    obstacleTypes = spriteDefinitionByType.original.obstacles;
    this.addCloud();

    // Multiple Horizon lines
    for (let i = 0; i < Runner.spriteDefinition.lines.length; i++) {
      this.horizonLines.push(
          new HorizonLine(this.canvas, Runner.spriteDefinition.lines[i]));
    }

    this.nightMode =
        new NightMode(this.canvas, this.spritePos.moon, this.dimensions.height);
  }

  /**
   * Update obstacle definitions based on the speed of the game.
   */
  adjustObstacleSpeed() {
    for (let i = 0; i < obstacleTypes.length; i++) {
      if (Runner.slowDown) {
        obstacleTypes[i].multipleSpeed = obstacleTypes[i].multipleSpeed / 2;
        obstacleTypes[i].minGap *= 1.5;
        obstacleTypes[i].minSpeed = obstacleTypes[i].minSpeed / 2;

        // Convert variable y position obstacles to fixed.
        if (typeof (obstacleTypes[i].yPos) === 'object') {
          obstacleTypes[i].yPos = obstacleTypes[i].yPos[0];
          obstacleTypes[i].yPosMobile = obstacleTypes[i].yPos[0];
        }
      }
    }
  }

  /**
   * Update sprites to correspond to change in sprite sheet.
   * @param {number} spritePos
   */
  enableAltGameMode(spritePos) {
    // Clear existing horizon objects.
    this.clouds = [];
    this.backgroundEls = [];

    this.altGameModeActive = true;
    this.spritePos = spritePos;

    obstacleTypes = Runner.spriteDefinition.obstacles;
    this.adjustObstacleSpeed();

    setMaxObstacleGapCoefficient(Runner.spriteDefinition.maxGapCoefficient);
    setMaxObstacleLength(Runner.spriteDefinition.maxObstacleLength);

    setBackgroundElGlobalConfig(Runner.spriteDefinition.backgroundElConfig);

    this.horizonLines = [];
    for (let i = 0; i < Runner.spriteDefinition.lines.length; i++) {
      this.horizonLines.push(
          new HorizonLine(this.canvas, Runner.spriteDefinition.lines[i]));
    }
    this.reset();
  }

  /**
   * @param {number} deltaTime
   * @param {number} currentSpeed
   * @param {boolean} updateObstacles Used as an override to prevent
   *     the obstacles from being updated / added. This happens in the
   *     ease in section.
   * @param {boolean} showNightMode Night mode activated.
   */
  update(deltaTime, currentSpeed, updateObstacles, showNightMode) {
    this.runningTime += deltaTime;

    if (this.altGameModeActive) {
      this.updateBackgroundEls(deltaTime, currentSpeed);
    }

    for (let i = 0; i < this.horizonLines.length; i++) {
      this.horizonLines[i].update(deltaTime, currentSpeed);
    }

    if (!this.altGameModeActive || Runner.spriteDefinition.hasClouds) {
      this.nightMode.update(showNightMode);
      this.updateClouds(deltaTime, currentSpeed);
    }

    if (updateObstacles) {
      this.updateObstacles(deltaTime, currentSpeed);
    }
  }

  /**
   * Update background element positions. Also handles creating new elements.
   * @param {number} elSpeed
   * @param {Array<Object>} bgElArray
   * @param {number} maxBgEl
   * @param {Function} bgElAddFunction
   * @param {number} frequency
   */
  updateBackgroundEl(elSpeed, bgElArray, maxBgEl, bgElAddFunction, frequency) {
    const numElements = bgElArray.length;

    if (numElements) {
      for (let i = numElements - 1; i >= 0; i--) {
        bgElArray[i].update(elSpeed);
      }

      const lastEl = bgElArray[numElements - 1];

      // Check for adding a new element.
      if (numElements < maxBgEl &&
          (this.dimensions.width - lastEl.xPos) > lastEl.gap &&
          frequency > Math.random()) {
        bgElAddFunction();
      }
    } else {
      bgElAddFunction();
    }
  }

  /**
   * Update the cloud positions.
   * @param {number} deltaTime
   * @param {number} speed
   */
  updateClouds(deltaTime, speed) {
    const elSpeed = this.cloudSpeed / 1000 * deltaTime * speed;
    this.updateBackgroundEl(
        elSpeed, this.clouds, this.config.MAX_CLOUDS, this.addCloud.bind(this),
        this.cloudFrequency);

    // Remove expired elements.
    this.clouds = this.clouds.filter((obj) => !obj.remove);
  }

  /**
   * Update the background element positions.
   * @param {number} deltaTime
   * @param {number} speed
   */
  updateBackgroundEls(deltaTime, speed) {
    this.updateBackgroundEl(
        deltaTime, this.backgroundEls, getBackgroundElGlobalConfig().maxBgEls,
        this.addBackgroundEl.bind(this), this.cloudFrequency);

    // Remove expired elements.
    this.backgroundEls = this.backgroundEls.filter((obj) => !obj.remove);
  }

  /**
   * Update the obstacle positions.
   * @param {number} deltaTime
   * @param {number} currentSpeed
   */
  updateObstacles(deltaTime, currentSpeed) {
    const updatedObstacles = this.obstacles.slice(0);

    for (let i = 0; i < this.obstacles.length; i++) {
      const obstacle = this.obstacles[i];
      obstacle.update(deltaTime, currentSpeed);

      // Clean up existing obstacles.
      if (obstacle.remove) {
        updatedObstacles.shift();
      }
    }
    this.obstacles = updatedObstacles;

    if (this.obstacles.length > 0) {
      const lastObstacle = this.obstacles[this.obstacles.length - 1];

      if (lastObstacle && !lastObstacle.followingObstacleCreated &&
          lastObstacle.isVisible() &&
          (lastObstacle.xPos + lastObstacle.width + lastObstacle.gap) <
              this.dimensions.width) {
        this.addNewObstacle(currentSpeed);
        lastObstacle.followingObstacleCreated = true;
      }
    } else {
      // Create new obstacles.
      this.addNewObstacle(currentSpeed);
    }
  }

  removeFirstObstacle() {
    this.obstacles.shift();
  }

  /**
   * Add a new obstacle.
   * @param {number} currentSpeed
   */
  addNewObstacle(currentSpeed) {
    const obstacleCount =
        obstacleTypes[obstacleTypes.length - 1].type !== 'collectable' ||
            (Runner.isAltGameModeEnabled() && !this.altGameModeActive ||
             this.altGameModeActive) ?
        obstacleTypes.length - 1 :
        obstacleTypes.length - 2;
    const obstacleTypeIndex =
        obstacleCount > 0 ? getRandomNum(0, obstacleCount) : 0;
    const obstacleType = obstacleTypes[obstacleTypeIndex];

    // Check for multiples of the same type of obstacle.
    // Also check obstacle is available at current speed.
    if ((obstacleCount > 0 && this.duplicateObstacleCheck(obstacleType.type)) ||
        currentSpeed < obstacleType.minSpeed) {
      this.addNewObstacle(currentSpeed);
    } else {
      const obstacleSpritePos = this.spritePos[obstacleType.type];

      this.obstacles.push(new Obstacle(
          this.canvasCtx, obstacleType, obstacleSpritePos, this.dimensions,
          this.gapCoefficient, currentSpeed, obstacleType.width,
          this.altGameModeActive));

      this.obstacleHistory.unshift(obstacleType.type);

      if (this.obstacleHistory.length > 1) {
        this.obstacleHistory.splice(Runner.config.MAX_OBSTACLE_DUPLICATION);
      }
    }
  }

  /**
   * Returns whether the previous two obstacles are the same as the next one.
   * Maximum duplication is set in config value MAX_OBSTACLE_DUPLICATION.
   * @return {boolean}
   */
  duplicateObstacleCheck(nextObstacleType) {
    let duplicateCount = 0;

    for (let i = 0; i < this.obstacleHistory.length; i++) {
      duplicateCount =
          this.obstacleHistory[i] === nextObstacleType ? duplicateCount + 1 : 0;
    }
    return duplicateCount >= Runner.config.MAX_OBSTACLE_DUPLICATION;
  }

  /**
   * Reset the horizon layer.
   * Remove existing obstacles and reposition the horizon line.
   */
  reset() {
    this.obstacles = [];
    for (let l = 0; l < this.horizonLines.length; l++) {
      this.horizonLines[l].reset();
    }

    this.nightMode.reset();
  }

  /**
   * Update the canvas width and scaling.
   * @param {number} width Canvas width.
   * @param {number} height Canvas height.
   */
  resize(width, height) {
    this.canvas.width = width;
    this.canvas.height = height;
  }

  /**
   * Add a new cloud to the horizon.
   */
  addCloud() {
    this.clouds.push(
        new Cloud(this.canvas, this.spritePos.cloud, this.dimensions.width));
  }

  /**
   * Add a random background element to the horizon.
   */
  addBackgroundEl() {
    const backgroundElTypes = Object.keys(Runner.spriteDefinition.backgroundEl);

    if (backgroundElTypes.length > 0) {
      let index = getRandomNum(0, backgroundElTypes.length - 1);
      let type = backgroundElTypes[index];

      // Add variation if available.
      while (type === this.lastEl && backgroundElTypes.length > 1) {
        index = getRandomNum(0, backgroundElTypes.length - 1);
        type = backgroundElTypes[index];
      }

      this.lastEl = type;
      this.backgroundEls.push(new BackgroundEl(
          this.canvas, this.spritePos.backgroundEl, this.dimensions.width,
          type));
    }
  }
}

/**
 * Horizon config.
 * @enum {number}
 */
Horizon.config = {
  BG_CLOUD_SPEED: 0.2,
  BUMPY_THRESHOLD: .3,
  CLOUD_FREQUENCY: .5,
  HORIZON_HEIGHT: 16,
  MAX_CLOUDS: 6,
};