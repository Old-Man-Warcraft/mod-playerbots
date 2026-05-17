-- Ensure playerbots_random_bots supports upsert writes by enforcing a unique owner/bot/event key.

DELETE `older`
FROM `playerbots_random_bots` `older`
INNER JOIN `playerbots_random_bots` `newer`
    ON `older`.`owner` = `newer`.`owner`
    AND `older`.`bot` = `newer`.`bot`
    AND `older`.`event` <=> `newer`.`event`
    AND `older`.`id` < `newer`.`id`;

SET @has_unique_owner_bot_event := (
  SELECT COUNT(1)
  FROM `INFORMATION_SCHEMA`.`STATISTICS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'playerbots_random_bots'
    AND `INDEX_NAME` = 'owner_bot_event'
    AND `NON_UNIQUE` = 0
);

SET @has_legacy_idx_owner_bot_event := (
  SELECT COUNT(1)
  FROM `INFORMATION_SCHEMA`.`STATISTICS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'playerbots_random_bots'
    AND `INDEX_NAME` = 'idx_owner_bot_event'
);

SET @has_nonunique_owner_bot_event := (
  SELECT COUNT(1)
  FROM `INFORMATION_SCHEMA`.`STATISTICS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'playerbots_random_bots'
    AND `INDEX_NAME` = 'owner_bot_event'
    AND `NON_UNIQUE` = 1
);

SET @drop_legacy_idx_sql := IF(@has_legacy_idx_owner_bot_event > 0,
  'ALTER TABLE `playerbots_random_bots` DROP INDEX `idx_owner_bot_event`;',
  'SELECT ''Legacy idx_owner_bot_event already absent'';'
);

PREPARE `stmt` FROM @drop_legacy_idx_sql;
EXECUTE `stmt`;
DEALLOCATE PREPARE `stmt`;

SET @drop_nonunique_owner_bot_event_sql := IF(@has_nonunique_owner_bot_event > 0,
  'ALTER TABLE `playerbots_random_bots` DROP INDEX `owner_bot_event`;',
  'SELECT ''Non-unique owner_bot_event already absent'';'
);

PREPARE `stmt` FROM @drop_nonunique_owner_bot_event_sql;
EXECUTE `stmt`;
DEALLOCATE PREPARE `stmt`;

SET @add_unique_owner_bot_event_sql := IF(@has_unique_owner_bot_event = 0,
  'ALTER TABLE `playerbots_random_bots` ADD UNIQUE INDEX `owner_bot_event` (`owner`, `bot`, `event`);',
  'SELECT ''Unique owner_bot_event already exists'';'
);

PREPARE `stmt` FROM @add_unique_owner_bot_event_sql;
EXECUTE `stmt`;
DEALLOCATE PREPARE `stmt`;