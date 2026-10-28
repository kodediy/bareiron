#ifndef H_ENTITYMETA
#define H_ENTITYMETA

// Entity metadata of Minecraft 1.21.8 (protocol 772): which index holds what,
// and the serializer type of each. Not in the server's reports, so taken from
// PrismarineJS minecraft-data, data/pc/1.21.8/entities.json and protocol.json
// (consulted 2026-10-28). The minecraft.wiki page "Java Edition protocol/Entity
// metadata" documents a newer version by now: there AgeableMob gained index 17
// (age locked), which moves every animal's own fields up by one.

// Serializer types
#define META_T_BYTE        0
#define META_T_VARINT      1
#define META_T_FLOAT       3
#define META_T_SLOT        7
#define META_T_BOOLEAN     8
#define META_T_BLOCK_STATE 14
#define META_T_POSE        21

// Entity
#define META_FLAGS         0   // byte: 0x01 on fire, 0x02 sneaking, 0x08 sprinting, 0x20 invisible
#define META_POSE          6   // pose
#define META_FLAG_ON_FIRE  0x01

// LivingEntity
#define META_LIVING_FLAGS  8   // byte: 0x01 hand active (drawing a bow)
#define META_HEALTH        9   // float
#define META_SLEEPING_POS  14  // optional block pos

// Mob
#define META_MOB_FLAGS     15  // byte: 0x01 no AI, 0x02 left handed, 0x04 aggressive

// AgeableMob (cow, pig, sheep, chicken) and Zombie (zombie, husk, zombie villager)
#define META_BABY          16  // boolean

// Creeper
#define META_CREEPER_STATE 16  // varint: -1 idle, 1 fusing
#define META_CREEPER_POWER 17  // boolean
#define META_CREEPER_LIT   18  // boolean

// Spider
#define META_SPIDER_FLAGS  16  // byte: 0x01 climbing

// Sheep
#define META_SHEEP_WOOL    17  // byte: 0x0F colour, 0x10 sheared

// Arrow
#define META_ARROW_FLAGS   8   // byte: 0x01 critical
#define META_ARROW_GROUND  10  // boolean

// Item entity
#define META_ITEM          8   // slot

// Primed TNT
#define META_TNT_FUSE      8   // varint

// Poses
#define POSE_STANDING      0
#define POSE_SLEEPING      2

#endif
