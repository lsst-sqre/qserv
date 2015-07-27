-- MySQL Script generated by MySQL Workbench
-- Mon Jul 20 18:26:42 2015
-- Model: New Model    Version: 1.0
-- MySQL Workbench Forward Engineering

SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0;
SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0;
SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='TRADITIONAL,ALLOW_INVALID_DATES';

-- -----------------------------------------------------
-- Schema qservCssData
-- -----------------------------------------------------

-- -----------------------------------------------------
-- Schema qservCssData
-- -----------------------------------------------------
CREATE SCHEMA IF NOT EXISTS `qservCssData` DEFAULT CHARACTER SET utf8 COLLATE utf8_general_ci ;
USE `qservCssData` ;

-- -----------------------------------------------------
-- Table `qservCssData`
-- -----------------------------------------------------
CREATE TABLE IF NOT EXISTS `cssData` (
  `kvKey` CHAR(255) NOT NULL COMMENT 'slash delimited path for key',
  `kvVal` TEXT(1024) NULL COMMENT 'value for key',
  `parentKey` CHAR(255) NULL COMMENT 'kvKey of the parent node',
  PRIMARY KEY (`kvKey`))
ENGINE = InnoDB
COMMENT = 'Table for kv data.';


SET SQL_MODE=@OLD_SQL_MODE;
SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS;
SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS;
