-- Fixes applied on top of the Feb/2016 official dump (openlastchaos/lastchaos-database) so the
-- 2009 servers (v2009/Server Source, USA build) run on it. Only touches the lc2009_* databases
-- (the newproject_* ones belong to the 2015 port). Idempotent; run by v2009/scripts/setup-db.sh.
--
-- Code-side fixes that go with these (instead of schema changes) are in the v2009 sources:
-- t_bigpet.a_Mdefence and t_castle.a_owner_char_index spellings, explicit column lists for the
-- Helper's t_extend_guild/t_extend_guildmember INSERT ... SELECT, the sharded t_purchase0N, and
-- SET SESSION sql_mode='' on every connection (2009 INSERTs omit NOT NULL datetime columns).

-- ---------------------------------------------------------------------------------------------
-- Schema
-- ---------------------------------------------------------------------------------------------

-- Read and saved with the character (GameServer DBThread.cpp); 2016 only has a_guildindate.
ALTER TABLE lc2009_db.t_characters ADD COLUMN IF NOT EXISTS a_guildoutdate INT NOT NULL DEFAULT 0;

-- ---------------------------------------------------------------------------------------------
-- Data
-- ---------------------------------------------------------------------------------------------

-- Client version window: the 2009 client sends (vtm.brn value - 27) / 3 = 1648 with the BR2014
-- install, 700 by default and on reconnect. The dump already has 0..25000; keep it explicit.
UPDATE lc2009_data.t_clientversion SET a_min = LEAST(a_min, 700), a_max = GREATEST(a_max, 1648);

-- Lacarette time (Connector LC_TIME): base row (a_server 0) and server group 1. The table comes
-- from newproject_db_auth-unused.sql and is empty; without rows the clock restarts at 0.
INSERT IGNORE INTO lc2009_auth.t_lc_time (a_server, a_time) VALUES (0, 0), (1, 0);

-- Test account teste2009 / teste2009. bg_user is the portal table (plaintext passwd) that both
-- LoginServer and Connector check; t_users.a_portal_index = bg_user.user_code (the Connector
-- would also create the t_users row on first login).
INSERT IGNORE INTO lc2009_auth.bg_user (user_code, user_id, passwd, active_time, create_date)
  VALUES (1, 'teste2009', 'teste2009', NOW(), NOW());
INSERT IGNORE INTO lc2009_auth.t_users (a_idname, a_passwd, a_portal_index, a_regi_date, a_end_date, a_enable)
  VALUES ('teste2009', '', 1, NOW(), NOW(), 1);

-- ---------------------------------------------------------------------------------------------
-- 2016 content clamped to what the 2009 code supports (audit of every startup loader of the USA
-- build against lc2009_data, 2026-10-19). Row counts are those of the Feb/2016 dump. The matching
-- code guards (Option.cpp, NPCProto.cpp, doFuncItem.cpp, doFuncExAPet.cpp) stop overflows that
-- these updates do not cover; the updates keep the data meaningful.
-- ---------------------------------------------------------------------------------------------

-- t_option.a_level/a_prob: 42 of 113 options list up to 36 levels; COptionProto keeps
-- OPTION_MAX_LEVEL (7) and the loader wrote past it (heap corruption). Keep the first 7.
UPDATE lc2009_data.t_option
  SET a_level = SUBSTRING_INDEX(TRIM(a_level), ' ', 7), a_prob = SUBSTRING_INDEX(TRIM(a_prob), ' ', 7)
  WHERE LENGTH(TRIM(a_level)) - LENGTH(REPLACE(TRIM(a_level), ' ', '')) + 1 > 7
     OR LENGTH(TRIM(a_prob)) - LENGTH(REPLACE(TRIM(a_prob), ' ', '')) + 1 > 7;

-- Job numbering: 2016 has 6 NightShadow, 7 EX-Rogue, 8 EX-Mage, 10 pet, 11 attack pet; the 2009
-- USA build (no NIGHT_SHADOW) has 6 pet, 7 attack pet. The remap is not idempotent by itself, so
-- it runs once, guarded by a row in t_lc2009_fixes.
CREATE TABLE IF NOT EXISTS lc2009_data.t_lc2009_fixes (
  a_name varchar(64) NOT NULL PRIMARY KEY,
  a_date datetime NOT NULL
);
-- t_quest: 30 quests need jobs 6-8 (Lorraine, NPC 139, would offer 53 quests; QUEST_MAX_NPC 50).
UPDATE lc2009_data.t_quest SET a_enable = 0 WHERE a_need_job BETWEEN 6 AND 8 AND a_enable = 1
  AND NOT EXISTS (SELECT 1 FROM lc2009_data.t_lc2009_fixes WHERE a_name = 'job_remap');
-- t_skill: 32 NightShadow + 64 EX-Rogue + 71 EX-Mage skills go to job 99 (matches no class);
-- 28 pet (10 -> 6) and 346 attack-pet (11 -> 7) skills remapped.
UPDATE lc2009_data.t_skill SET a_job = CASE a_job WHEN 10 THEN 6 WHEN 11 THEN 7 ELSE 99 END
  WHERE a_job IN (6, 7, 8, 10, 11)
  AND NOT EXISTS (SELECT 1 FROM lc2009_data.t_lc2009_fixes WHERE a_name = 'job_remap');
-- t_item.a_job_flag bits: keep the 6 classes, pet bit 10 -> 6, attack pet bit 11 -> 7, drop the
-- rest (bits 6-8: NightShadow/EX classes).
UPDATE lc2009_data.t_item
  SET a_job_flag = (a_job_flag & 63) | IF(a_job_flag & 1024, 64, 0) | IF(a_job_flag & 2048, 128, 0)
  WHERE a_job_flag & ~63
  AND NOT EXISTS (SELECT 1 FROM lc2009_data.t_lc2009_fixes WHERE a_name = 'job_remap');
INSERT IGNORE INTO lc2009_data.t_lc2009_fixes (a_name, a_date) VALUES ('job_remap', NOW());

-- Back wings (a_wearing 11, 129 enabled items): 2009 has MAX_WEARING 11 slots (0-10).
UPDATE lc2009_data.t_item SET a_enable = 0 WHERE a_wearing >= 11 AND a_enable = 1;

-- Zone 20 (Egeha dungeon 10F) declares 2 respawn points but lists one; the second was (0,0).
UPDATE lc2009_data.t_zonedata SET a_poscount = 1 WHERE a_zone_index = 20 AND a_poscount = 2;

-- Cash shop entries 54-59 sell disabled items 1301-1306 ("Can't Create Item" at startup; the
-- purchase rollback path dereferences the missing proto).
DELETE FROM lc2009_data.t_ct_item WHERE a_ctid BETWEEN 54 AND 59;
DELETE FROM lc2009_data.t_catalog WHERE a_ctid BETWEEN 54 AND 59;

-- Attack pet proto 10 (t_bigpet) is made from disabled item 5076; creating it would crash.
UPDATE lc2009_data.t_bigpet SET a_enable = 0 WHERE a_index = 10;
