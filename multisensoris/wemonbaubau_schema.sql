CREATE DATABASE IF NOT EXISTS `Wemon_BauBau`
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

USE `Wemon_BauBau`;

CREATE TABLE IF NOT EXISTS `weather_monitoring` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `windir` INT NOT NULL,
  `windavg` DOUBLE NOT NULL,
  `windmax` DOUBLE NOT NULL,
  `rain1h` DOUBLE NOT NULL,
  `rain24h` DOUBLE NOT NULL,
  `suhu` DOUBLE NOT NULL,
  `humidity` DOUBLE NOT NULL,
  `pressure` DOUBLE NOT NULL,
  `distance` INT NOT NULL,
  `waterheight` INT NOT NULL,
  `waveheight` INT NOT NULL,
  `lidar_sample_interval_ms` SMALLINT UNSIGNED NULL,
  `distance_01` INT NULL,
  `distance_02` INT NULL,
  `distance_03` INT NULL,
  `distance_04` INT NULL,
  `distance_05` INT NULL,
  `distance_06` INT NULL,
  `distance_07` INT NULL,
  `distance_08` INT NULL,
  `distance_09` INT NULL,
  `distance_10` INT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_created_at` (`created_at`)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `water_flow` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `enc_rpm` DOUBLE NOT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_created_at` (`created_at`)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `water_direction` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `ang_dir` INT NOT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_created_at` (`created_at`)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;
