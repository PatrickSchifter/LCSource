-- Schema fixes applied on top of openlastchaos/lastchaos-database so it matches this server source.
-- Safe to run more than once (MariaDB).
--   mysql < server-config/db-fixes.sql

-- GameServer reads and saves it (DBProcess_SelectChar.cpp, DBManager_SaveCharacterInfo.cpp);
-- without it entering the game aborts the GameServer.
ALTER TABLE newproject_db.t_characters ADD COLUMN IF NOT EXISTS a_guildoutdate INT NOT NULL DEFAULT 0;
