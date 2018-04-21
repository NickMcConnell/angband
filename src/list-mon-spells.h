/**
 * \file list-mon-spells.h
 * \brief List of monster spell flags 
 *
 * Changing flag order will break savefiles.  Flags
 * below start from 0 on line 20, so a flag's sequence number is its line
 * number minus 20.
 *
 * Fields:
 * name - spell name
 * type - spell type
 */
/* 	name		type*/
RSF(NONE,		0)
RSF(SHRIEK,		RST_ANNOY | RST_INNATE)
RSF(ARROW_1,	RST_BOLT | RST_INNATE)
RSF(ARROW_2,	RST_BOLT | RST_INNATE)
RSF(ARROW_3,	RST_BOLT | RST_INNATE)
RSF(ARROW_4,	RST_BOLT | RST_INNATE)
RSF(BR_ACID,	RST_BREATH | RST_INNATE)
RSF(BR_ELEC,	RST_BREATH | RST_INNATE)
RSF(BR_FIRE,	RST_BREATH | RST_INNATE)
RSF(BR_COLD,	RST_BREATH | RST_INNATE)
RSF(BR_POIS,	RST_BREATH | RST_INNATE)
RSF(BR_NETH,	RST_BREATH | RST_INNATE)
RSF(BR_LIGHT,	RST_BREATH | RST_INNATE)
RSF(BR_DARK,	RST_BREATH | RST_INNATE)
RSF(BR_SOUN,	RST_BREATH | RST_INNATE)
RSF(BR_CHAO,	RST_BREATH | RST_INNATE)
RSF(BR_DISE,	RST_BREATH | RST_INNATE)
RSF(BR_NEXU,	RST_BREATH | RST_INNATE)
RSF(BR_TIME,	RST_BREATH | RST_INNATE)
RSF(BR_INER,	RST_BREATH | RST_INNATE)
RSF(BR_GRAV,	RST_BREATH | RST_INNATE)
RSF(BR_SHAR,	RST_BREATH | RST_INNATE)
RSF(BR_PLAS,	RST_BREATH | RST_INNATE)
RSF(BR_WALL,	RST_BREATH | RST_INNATE)
RSF(BR_MANA,	RST_BREATH | RST_INNATE)
RSF(BOULDER,	RST_BOLT | RST_INNATE)
RSF(BA_ACID,	RST_BALL)
RSF(BA_ELEC,	RST_BALL)
RSF(BA_FIRE,	RST_BALL)
RSF(BA_COLD,	RST_BALL)
RSF(BA_POIS,	RST_BALL)
RSF(BA_NETH,	RST_BALL)
RSF(BA_WATE,	RST_BALL)
RSF(BA_MANA,	RST_BALL)
RSF(BA_DARK,	RST_BALL)
RSF(DRAIN_MANA,	RST_ANNOY)
RSF(MIND_BLAST,	RST_ATTACK | RST_ANNOY)
RSF(BRAIN_SMASH,RST_ATTACK | RST_ANNOY)
RSF(CAUSE_1,	RST_ATTACK)
RSF(CAUSE_2,	RST_ATTACK)
RSF(CAUSE_3,	RST_ATTACK)
RSF(CAUSE_4,	RST_ATTACK)
RSF(BO_ACID,	RST_BOLT)
RSF(BO_ELEC,	RST_BOLT)
RSF(BO_FIRE,	RST_BOLT)
RSF(BO_COLD,	RST_BOLT)
RSF(BO_POIS,	RST_BOLT)
RSF(BO_NETH,	RST_BOLT)
RSF(BO_WATE,	RST_BOLT)
RSF(BO_MANA,	RST_BOLT)
RSF(BO_PLAS,	RST_BOLT)
RSF(BO_ICEE,	RST_BOLT)
RSF(MISSILE,	RST_BOLT)
RSF(SCARE,		RST_ANNOY)
RSF(BLIND,		RST_ANNOY)
RSF(CONF,		RST_ANNOY)
RSF(SLOW,		RST_ANNOY | RST_HASTE)
RSF(HOLD,		RST_ANNOY | RST_HASTE)
RSF(HASTE,		RST_HASTE)
RSF(HEAL,		RST_HEAL)
RSF(HEAL_KIN,	RST_HEAL_OTHER)
RSF(BLINK,		RST_TACTIC | RST_ESCAPE)
RSF(TPORT,		RST_ESCAPE)
RSF(TELE_TO,	RST_ANNOY)
RSF(TELE_AWAY,	RST_ESCAPE)
RSF(TELE_LEVEL,	RST_ESCAPE)
RSF(DARKNESS,	RST_ANNOY)
RSF(TRAPS,		RST_ANNOY)
RSF(FORGET,		RST_ANNOY)
RSF(S_KIN,		RST_SUMMON)
RSF(S_HI_DEMON,	RST_SUMMON)
RSF(S_MONSTER,	RST_SUMMON)
RSF(S_MONSTERS,	RST_SUMMON)
RSF(S_ANIMAL,	RST_SUMMON)
RSF(S_SPIDER,	RST_SUMMON)
RSF(S_HOUND,	RST_SUMMON)
RSF(S_HYDRA,	RST_SUMMON)
RSF(S_AINU,		RST_SUMMON)
RSF(S_DEMON,	RST_SUMMON)
RSF(S_UNDEAD,	RST_SUMMON)
RSF(S_DRAGON,	RST_SUMMON)
RSF(S_HI_UNDEAD,RST_SUMMON)
RSF(S_HI_DRAGON,RST_SUMMON)
RSF(S_WRAITH,	RST_SUMMON)
RSF(S_UNIQUE,	RST_SUMMON)
RSF(MAX,		0)
