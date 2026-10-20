-- Zone 0 (old Juno) NPC positions and headings set by hand in game (/whereami and the user's
-- corrections). An NPC with heading r looks where a player with heading r looks.
-- Applied after zone0-town-facing.sql. Idempotent.
UPDATE t_npc_regen SET a_pos_x=1152.24, a_pos_z=962.45, a_pos_r=142.15, a_y_layer=0 WHERE a_index=3663376; -- 253 Animal Trainer (/whereami 142.15)
UPDATE t_npc_regen SET a_pos_x=1158.96, a_pos_z=950.77, a_pos_r=93.14, a_y_layer=0 WHERE a_index=3663343; -- 149 Storekeeper Bianca (53.14, then turned left twice by 20)
UPDATE t_npc_regen SET a_pos_r=-10, a_y_layer=0 WHERE a_index=3663380; -- 408 Estella (Leonor in BR): 80 from zone0-town-facing.sql, turned 90 to the right
UPDATE t_npc_regen SET a_pos_r=15, a_y_layer=0 WHERE a_index=3663838; -- 834 Phoenix Club Master (Ikki in BR): -75 from zone0-town-facing.sql, turned 90 to the left
UPDATE t_npc_regen SET a_pos_r=20, a_y_layer=0 WHERE a_index=3663309; -- 136 Captain Leon (Capitão Leon): -70 from zone0-town-facing.sql, turned 90 to the left
-- Randy, Gerel and Coen face the same way as Merchant Geres (shop 20, -80 from zone0-town-facing.sql).
UPDATE t_npc_regen SET a_pos_r=-80 WHERE a_index=3663377; -- 254 Domain Merchant (Randy)
UPDATE t_shop SET a_pos_r=-80 WHERE a_zone_num=0 AND a_keeper_idx=78; -- Scroll Trader (Vendedor Ambulante Gerel)
UPDATE t_shop SET a_pos_r=-80 WHERE a_zone_num=0 AND a_keeper_idx=226; -- Magical Shopkeeper (Armarinheiro Mágico Coen)
UPDATE t_shop SET a_pos_r=-80 WHERE a_zone_num=0 AND a_keeper_idx=517; -- Pet Merchant (Vendedor Polly): same as Geres
UPDATE t_npc_regen SET a_pos_r=-130 WHERE a_index=3663125; -- 55 Transmuter (Alkemist): -85 from zone0-town-facing.sql, turned 45 to the right
-- Rowel, Baran, Ferreiro and Aluguel de Arma face the same way as the Alkemist (-130).
UPDATE t_shop SET a_pos_r=-130 WHERE a_zone_num=0 AND a_keeper_idx=90; -- Armor Trader (Vendedor de Armaduras Rowel)
UPDATE t_shop SET a_pos_r=-130 WHERE a_zone_num=0 AND a_keeper_idx=33; -- Weapon Trader (Vendedor de Armas Baran)
UPDATE t_npc_regen SET a_pos_r=-130 WHERE a_index=3663188; -- 91 Miner Trainer (Ferreiro)
UPDATE t_shop SET a_pos_r=-130 WHERE a_zone_num=0 AND a_keeper_idx=272; -- Weapon Lending Dealer (Aluguel de Arma)
UPDATE t_shop SET a_pos_r=-140 WHERE a_zone_num=0 AND a_keeper_idx=33; -- Baran: 10 more to the right
UPDATE t_npc_regen SET a_pos_r=-140 WHERE a_index=3663188; -- Ferreiro: 10 more to the right
UPDATE t_shop SET a_pos_r=-175 WHERE a_zone_num=0 AND a_keeper_idx=272; -- Aluguel de Arma: 45 more to the right
