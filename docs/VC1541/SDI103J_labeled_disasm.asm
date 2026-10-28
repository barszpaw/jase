; SDI103J_ED3DC6CD.ROM -- labeled disassembly, 4096 bytes
; Ground truth from 1 INSTRTRACE log(s): 935 confirmed addresses
; '>>' = confirmed by real JASE execution this session (ground truth).
; '..' = statically decoded only (z80dis), NOT independently confirmed --
;        may be wrong if this is actually data the Z80 never fetched here.

  $3000: dbdf     IN A,(0x00df)                >> confirmed
  $3002: e6fb     AND 0xfb                     >> confirmed
  $3004: d3df     OUT (0x00df),A               >> confirmed
  $3006: c5       PUSH BC                      >> confirmed
  $3007: 0605     LD B,5                       >> confirmed
  $3009: 10fe     DJNZ 0x3009                  >> confirmed
  $300b: c1       POP BC                       >> confirmed
  $300c: c9       RET                          >> confirmed
  $300d: dbdf     IN A,(0x00df)                >> confirmed
  $300f: f604     OR 4                         >> confirmed
  $3011: d3df     OUT (0x00df),A               >> confirmed
  $3013: 18f1     JR 0x3006                    >> confirmed
  $3015: dbdf     IN A,(0x00df)                >> confirmed
  $3017: e6f7     AND 0xf7                     >> confirmed
  $3019: d3df     OUT (0x00df),A               >> confirmed
  $301b: 18e9     JR 0x3006                    >> confirmed
  $301d: dbdf     IN A,(0x00df)                >> confirmed
  $301f: f608     OR 8                         >> confirmed
  $3021: d3df     OUT (0x00df),A               >> confirmed
  $3023: 18e1     JR 0x3006                    >> confirmed
  $3025: c5       PUSH BC                      >> confirmed
  $3026: dbdf     IN A,(0x00df)                >> confirmed
  $3028: 5f       LD E,A                       >> confirmed
  $3029: 0605     LD B,5                       >> confirmed
  $302b: 10fe     DJNZ 0x302b                  >> confirmed
  $302d: dbdf     IN A,(0x00df)                >> confirmed
  $302f: bb       CP E                         >> confirmed
  $3030: 20f4     JR nz,0x3026                 >> confirmed
  $3032: c1       POP BC                       >> confirmed
  $3033: cb27     SLA A                        >> confirmed
  $3035: c9       RET                          >> confirmed
  $3036: 72       LD (HL),D                    >> confirmed
  $3037: 2b       DEC HL                       >> confirmed
  $3038: 2b       DEC HL                       >> confirmed
  $3039: 2b       DEC HL                       >> confirmed
  $303a: d1       POP DE                       >> confirmed
  $303b: c9       RET                          >> confirmed
  $303c: 92       SUB D                        .. static only
  $303d: 5c       LD E,H                       .. static only
  $303e: b6       OR (HL)                      >> confirmed
  $303f: 77       LD (HL),A                    >> confirmed
  $3040: a7       AND A                        >> confirmed
  $3041: d26530   JP nc,0x3065                 >> confirmed
  $3044: 3a955c   LD A,(0x5c95)                .. static only
  $3047: cda230   CALL 0x30a2                  .. static only
  $304a: dbdf     IN A,(0x00df)                .. static only
  $304c: e6fe     AND 0xfe                     .. static only
  $304e: d3df     OUT (0x00df),A               .. static only
  $3050: c9       RET                          .. static only
  $3051: cd6030   CALL IEC_UNLISTEN            >> confirmed
  $3054: cd0d30   CALL 0x300d                  >> confirmed
  $3057: dbdf     IN A,(0x00df)                >> confirmed
  $3059: f601     OR 1                         >> confirmed
  $305b: d3df     OUT (0x00df),A               >> confirmed
  $305d: 3e5f     LD A,0x5f                    >> confirmed
  $305f: 013e3f   LD BC,0x3f3e                 >> confirmed
  $3062: cd7a30   CALL 0x307a                  >> confirmed
  $3065: dbdf     IN A,(0x00df)                >> confirmed
  $3067: e6fe     AND 0xfe                     >> confirmed
  $3069: d3df     OUT (0x00df),A               >> confirmed
  $306b: 0610     LD B,0x10                    >> confirmed
  $306d: 10fe     DJNZ 0x306d                  >> confirmed
  $306f: cd0030   CALL 0x3000                  >> confirmed
  $3072: c31530   JP 0x3015                    >> confirmed
IEC_SEND_DEVNUM:
  $3075: f640     OR 0x40                      >> confirmed
  $3077: 01f620   LD BC,0x20f6                 >> confirmed
  $307a: f5       PUSH AF                      >> confirmed
  $307b: 3a945c   LD A,(0x5c94)                >> confirmed
  $307e: cb7f     BIT 7,A                      >> confirmed
  $3080: 2812     JR z,0x3094                  >> confirmed
  $3082: 37       SCF                          >> confirmed
  $3083: 21965c   LD HL,0x5c96                 >> confirmed
  $3086: cb1e     RR (HL)                      >> confirmed
  $3088: e5       PUSH HL                      >> confirmed
  $3089: cdd430   CALL 0x30d4                  >> confirmed
  $308c: e1       POP HL                       >> confirmed
  $308d: cb26     SLA (HL)                     >> confirmed
  $308f: 21945c   LD HL,0x5c94                 >> confirmed
  $3092: cb26     SLA (HL)                     >> confirmed
  $3094: f1       POP AF                       >> confirmed
  $3095: 32955c   LD (0x5c95),A                >> confirmed
  $3098: cd1530   CALL 0x3015                  >> confirmed
  $309b: fe3f     CP 0x3f                      >> confirmed
  $309d: 2003     JR nz,0x30a2                 >> confirmed
  $309f: cd0030   CALL 0x3000                  .. static only
  $30a2: dbdf     IN A,(0x00df)                >> confirmed
  $30a4: f601     OR 1                         >> confirmed
  $30a6: d3df     OUT (0x00df),A               >> confirmed
  $30a8: cd0630   CALL 0x3006                  >> confirmed
  $30ab: cd0d30   CALL 0x300d                  >> confirmed
  $30ae: cd1530   CALL 0x3015                  >> confirmed
  $30b1: 06ff     LD B,0xff                    >> confirmed
  $30b3: 10fe     DJNZ 0x30b3                  >> confirmed
  $30b5: 061b     LD B,0x1b                    >> confirmed
  $30b7: 10fe     DJNZ 0x30b7                  >> confirmed
  $30b9: c3d430   JP 0x30d4                    >> confirmed
  $30bc: 21945c   LD HL,0x5c94                 >> confirmed
  $30bf: cb7e     BIT 7,(HL)                   >> confirmed
  $30c1: 2005     JR nz,0x30c8                 >> confirmed
  $30c3: 37       SCF                          >> confirmed
  $30c4: cb1e     RR (HL)                      >> confirmed
  $30c6: 2007     JR nz,0x30cf                 >> confirmed
  $30c8: e5       PUSH HL                      >> confirmed
  $30c9: f5       PUSH AF                      >> confirmed
  $30ca: cdd430   CALL 0x30d4                  >> confirmed
  $30cd: f1       POP AF                       >> confirmed
  $30ce: e1       POP HL                       >> confirmed
  $30cf: 32955c   LD (0x5c95),A                >> confirmed
  $30d2: a7       AND A                        >> confirmed
  $30d3: c9       RET                          >> confirmed
  $30d4: 21955c   LD HL,0x5c95                 >> confirmed
  $30d7: 4e       LD C,(HL)                    >> confirmed
  $30d8: dbdf     IN A,(0x00df)                >> confirmed
  $30da: e6f7     AND 0xf7                     >> confirmed
  $30dc: d3df     OUT (0x00df),A               >> confirmed
  $30de: dbdf     IN A,(0x00df)                >> confirmed
  $30e0: b7       OR A                         >> confirmed
  $30e1: fa3630   JP m,0x3036                  >> confirmed
  $30e4: e6fb     AND 0xfb                     >> confirmed
  $30e6: d3df     OUT (0x00df),A               >> confirmed
  $30e8: 23       INC HL                       >> confirmed
  $30e9: cb7e     BIT 7,(HL)                   >> confirmed
  $30eb: 280a     JR z,0x30f7                  >> confirmed
  $30ed: d1       POP DE                       >> confirmed
  $30ee: c9       RET                          >> confirmed
  $30ef: 17       RLA                          .. static only
  $30f0: 30fb     JR nc,0x30ed                 .. static only
  $30f2: dbdf     IN A,(0x00df)                .. static only
  $30f4: 17       RLA                          .. static only
  $30f5: 38fb     JR c,0x30f2                  .. static only
  $30f7: dbdf     IN A,(0x00df)                >> confirmed
  $30f9: b7       OR A                         >> confirmed
  $30fa: f2f730   JP p,0x30f7                  >> confirmed
  $30fd: f604     OR 4                         >> confirmed
  $30ff: d3df     OUT (0x00df),A               >> confirmed
  $3101: 0608     LD B,8                       >> confirmed
  $3103: 1e08     LD E,8                       >> confirmed
  $3105: 53       LD D,E                       >> confirmed
  $3106: 15       DEC D                        >> confirmed
  $3107: 20fd     JR nz,0x3106                 >> confirmed
  $3109: dbdf     IN A,(0x00df)                >> confirmed
  $310b: b7       OR A                         >> confirmed
  $310c: f23930   JP p,0x3039                  >> confirmed
  $310f: e6f7     AND 0xf7                     >> confirmed
  $3111: cb09     RRC C                        >> confirmed
  $3113: 3802     JR c,0x3117                  >> confirmed
  $3115: f608     OR 8                         >> confirmed
  $3117: d3df     OUT (0x00df),A               >> confirmed
  $3119: 53       LD D,E                       >> confirmed
  $311a: 15       DEC D                        >> confirmed
  $311b: 20fd     JR nz,0x311a                 >> confirmed
  $311d: e6fb     AND 0xfb                     >> confirmed
  $311f: d3df     OUT (0x00df),A               >> confirmed
  $3121: 53       LD D,E                       >> confirmed
  $3122: 15       DEC D                        >> confirmed
  $3123: 20fd     JR nz,0x3122                 >> confirmed
  $3125: e6f7     AND 0xf7                     >> confirmed
  $3127: f604     OR 4                         >> confirmed
  $3129: d3df     OUT (0x00df),A               >> confirmed
  $312b: 10dc     DJNZ 0x3109                  >> confirmed
  $312d: 0681     LD B,0x81                    >> confirmed
  $312f: dbdf     IN A,(0x00df)                >> confirmed
  $3131: 17       RLA                          >> confirmed
  $3132: d0       RET nc                       >> confirmed
  $3133: 10fa     DJNZ 0x312f                  .. static only
  $3135: c33930   JP 0x3039                    .. static only
IEC_RECEIVE_BYTE:
  $3138: af       XOR A                        >> confirmed
  $3139: 21925c   LD HL,0x5c92                 >> confirmed
  $313c: 2e96     LD L,0x96                    >> confirmed
  $313e: 77       LD (HL),A                    >> confirmed
  $313f: cd0030   CALL 0x3000                  >> confirmed
  $3142: cd2530   CALL 0x3025                  >> confirmed
  $3145: f24231   JP p,0x3142                  >> confirmed
  $3148: 1614     LD D,0x14                    >> confirmed
  $314a: dbdf     IN A,(0x00df)                >> confirmed
  $314c: e6f7     AND 0xf7                     >> confirmed
  $314e: 00       NOP                          >> confirmed
  $314f: d3df     OUT (0x00df),A               >> confirmed
  $3151: dbdf     IN A,(0x00df)                >> confirmed
  $3153: e640     AND 0x40                     >> confirmed
  $3155: 281e     JR z,0x3175                  >> confirmed
  $3157: 15       DEC D                        .. static only
  $3158: 20f7     JR nz,0x3151                 .. static only
  $315a: 3a965c   LD A,(0x5c96)                .. static only
  $315d: b7       OR A                         .. static only
  $315e: 2805     JR z,0x3165                  .. static only
  $3160: 3e02     LD A,2                       .. static only
  $3162: c33b30   JP 0x303b                    .. static only
  $3165: cd1d30   CALL 0x301d                  .. static only
  $3168: cd0030   CALL 0x3000                  .. static only
  $316b: 21925c   LD HL,0x5c92                 .. static only
  $316e: cbf6     SET 6,(HL)                   .. static only
  $3170: 2e96     LD L,0x96                    .. static only
  $3172: 34       INC (HL)                     .. static only
  $3173: 20d3     JR nz,0x3148                 .. static only
  $3175: 3608     LD (HL),8                    >> confirmed
  $3177: dbdf     IN A,(0x00df)                >> confirmed
  $3179: 87       ADD A,A                      >> confirmed
  $317a: f27731   JP p,0x3177                  >> confirmed
  $317d: 21975c   LD HL,0x5c97                 >> confirmed
  $3180: cb1e     RR (HL)                      >> confirmed
  $3182: dbdf     IN A,(0x00df)                >> confirmed
  $3184: 87       ADD A,A                      >> confirmed
  $3185: fa8231   JP m,0x3182                  >> confirmed
  $3188: 21965c   LD HL,0x5c96                 >> confirmed
  $318b: 35       DEC (HL)                     >> confirmed
  $318c: 20e9     JR nz,0x3177                 >> confirmed
  $318e: cd1d30   CALL 0x301d                  >> confirmed
  $3191: 21925c   LD HL,0x5c92                 >> confirmed
  $3194: cb76     BIT 6,(HL)                   >> confirmed
  $3196: 2803     JR z,0x319b                  >> confirmed
  $3198: cd6b30   CALL 0x306b                  .. static only
  $319b: 3a975c   LD A,(0x5c97)                >> confirmed
  $319e: c9       RET                          >> confirmed
  $319f: 0603     LD B,3                       .. static only
  $31a1: 3a935c   LD A,(0x5c93)                .. static only
  $31a4: b7       OR A                         .. static only
  $31a5: 2802     JR z,PRINT_MSG_SKIP_LOOP     .. static only
  $31a7: 0605     LD B,5                       .. static only
PRINT_MSG_SKIP_LOOP:
  $31a9: af       XOR A                        >> confirmed
  $31aa: 21e331   LD HL,0x31e3                 >> confirmed
  $31ad: b8       CP B                         >> confirmed
  $31ae: 2807     JR z,0x31b7                  >> confirmed
  $31b0: cb7e     BIT 7,(HL)                   >> confirmed
  $31b2: 23       INC HL                       >> confirmed
  $31b3: 28fb     JR z,0x31b0                  >> confirmed
  $31b5: 10f9     DJNZ 0x31b0                  >> confirmed
  $31b7: 7e       LD A,(HL)                    >> confirmed
  $31b8: cb7f     BIT 7,A                      >> confirmed
  $31ba: 2006     JR nz,0x31c2                 >> confirmed
  $31bc: cd2b3e   CALL 0x3e2b                  >> confirmed
  $31bf: 23       INC HL                       >> confirmed
  $31c0: 18f5     JR 0x31b7                    >> confirmed
  $31c2: cbbf     RES 7,A                      >> confirmed
  $31c4: c32b3e   JP 0x3e2b                    >> confirmed
  $31c7: 0604     LD B,4                       .. static only
  $31c9: cda931   CALL PRINT_MSG_SKIP_LOOP     >> confirmed
  $31cc: 3a9c5c   LD A,(0x5c9c)                >> confirmed
  $31cf: 47       LD B,A                       >> confirmed
  $31d0: 2a9e5c   LD HL,(0x5c9e)               >> confirmed
  $31d3: 7e       LD A,(HL)                    >> confirmed
  $31d4: cd2b3e   CALL 0x3e2b                  >> confirmed
  $31d7: 23       INC HL                       >> confirmed
  $31d8: 10f9     DJNZ 0x31d3                  >> confirmed
  $31da: 3e0d     LD A,13                      >> confirmed
  $31dc: c32b3e   JP 0x3e2b                    >> confirmed
  $31df: 0602     LD B,2                       >> confirmed
  $31e1: 18e6     JR 0x31c9                    >> confirmed
; --- DATA REGION MSG_TABLE_AND_BANNER [$31e3-$3338] (CONFIRMED byte-exact) ---
  $31e3: DEFM '\rFILE NOT FOUND' (last char flagged, 0xc4)
  $31f2: DEFM '\rDEVICE NOT PRESENT' (last char flagged, 0xd4)
  $3205: DEFM '\rSEARCHING FOR ' (last char flagged, 0xa0)
  $3214: DEFM '\rLOADING ' (last char flagged, 0xa0)
  $321d: DEFM '\rSAVING ' (last char flagged, 0xa0)
  $3225: DEFM '\rVERIFYING ' (last char flagged, 0xa0)
  $3230: DEFM 'ERROR\r' (last char flagged, 0x8d)
  $3236: DEFM '\rFILE ALREADY EXISTS \r' (last char flagged, 0x8d)
  $324c: DEFM '\rNOW MERGING\r' (last char flagged, 0x8d)
  $3259: DEFM '\rWRONG FILE TYPE\r' (last char flagged, 0x8d)
  $326a: DEFM '\rFILES NOT EQUAL IN LENGHT\r' (last char flagged, 0x8d)
  $3285: DEFM '\rEPROM NOT PRESENT\r' (last char flagged, 0x8d)
  $3298: DEFM '\rROUTINE NOT PRESENT\r' (last char flagged, 0x8d)
  $32ad: DEFM '    SPECTRUM DISK INTERFACE\x17\x0b\x00DOS V1.03J\r\rAutori HARDVERA: URO\x10' (last char flagged, 0x90)
  $32ec: DEFM 'EVI\x11' (last char flagged, 0x91)
  $32f0: DEFM ' MILAN\x17\x07\x00SOFTVERA: GEREN\x12' (last char flagged, 0x92)
  $3309: DEFB [73, 82, 32, 73, 86, 65, 78, 13, 13, 13, 24, 60, 64, 60, 2, 66, 60, 0, 8, 60, 66, 64, 64, 66, 60, 0, 24, 60, 66, 64, 64, 66, 60, 0, 6, 6, 33, 6, 0, 33, 6, 1, 33, 6, 11, 33, 6, 12] (trailing, unterminated)
; --- end MSG_TABLE_AND_BANNER ---

ERR_DISPATCH:
  $3339: c5       PUSH BC                      >> confirmed
  $333a: cd763e   CALL CALL1601_A_FE_RET       >> confirmed
  $333d: c1       POP BC                       >> confirmed
  $333e: cda931   CALL PRINT_MSG_SKIP_LOOP     >> confirmed
  $3341: cd713e   CALL 0x3e71                  >> confirmed
  $3344: cd5130   CALL 0x3051                  >> confirmed
  $3347: cdf234   CALL 0x34f2                  >> confirmed
  $334a: cd523c   CALL 0x3c52                  >> confirmed
  $334d: c37c3e   JP 0x3e7c                    >> confirmed
IEC_SEND_SECONDARY_RELEASE_ATN:
  $3350: 32955c   LD (0x5c95),A                >> confirmed
  $3353: cdab30   CALL 0x30ab                  >> confirmed
  $3356: dbdf     IN A,(0x00df)                >> confirmed
  $3358: e6fe     AND 0xfe                     >> confirmed
  $335a: d3df     OUT (0x00df),A               >> confirmed
  $335c: c9       RET                          >> confirmed
  $335d: 32955c   LD (0x5c95),A                >> confirmed
  $3360: cdab30   CALL 0x30ab                  >> confirmed
  $3363: e3       EX (SP),HL                   >> confirmed
  $3364: d9       EXX                          >> confirmed
  $3365: ed53655c LD (0x5c65),DE               >> confirmed
  $3369: d9       EXX                          >> confirmed
  $336a: 7e       LD A,(HL)                    >> confirmed
  $336b: 23       INC HL                       >> confirmed
  $336c: e5       PUSH HL                      >> confirmed
  $336d: a7       AND A                        >> confirmed
  $336e: f28033   JP p,0x3380                  >> confirmed
  $3371: 57       LD D,A                       >> confirmed
  $3372: e660     AND 0x60                     >> confirmed
  $3374: 0f       RRCA                         >> confirmed
  $3375: 0f       RRCA                         >> confirmed
  $3376: 0f       RRCA                         >> confirmed
  $3377: 0f       RRCA                         >> confirmed
  $3378: c67c     ADD A,0x7c                   >> confirmed
  $337a: 6f       LD L,A                       >> confirmed
  $337b: 7a       LD A,D                       >> confirmed
  $337c: e61f     AND 0x1f                     >> confirmed
  $337e: 180e     JR 0x338e                    >> confirmed
  $3380: fe18     CP 0x18                      >> confirmed
  $3382: 3008     JR nc,0x338c                 >> confirmed
  $3384: d9       EXX                          >> confirmed
  $3385: 01fbff   LD BC,0xfffb                 >> confirmed
  $3388: 54       LD D,H                       >> confirmed
  $3389: 5d       LD E,L                       >> confirmed
  $338a: 09       ADD HL,BC                    >> confirmed
  $338b: d9       EXX                          >> confirmed
  $338c: 07       RLCA                         >> confirmed
  $338d: 6f       LD L,A                       >> confirmed
  $338e: 11d732   LD DE,0x32d7                 >> confirmed
  $3391: 2600     LD H,0                       >> confirmed
  $3393: 19       ADD HL,DE                    >> confirmed
  $3394: 5e       LD E,(HL)                    >> confirmed
  $3395: 23       INC HL                       >> confirmed
  $3396: 56       LD D,(HL)                    >> confirmed
  $3397: 216533   LD HL,0x3365                 >> confirmed
  $339a: e3       EX (SP),HL                   >> confirmed
  $339b: d5       PUSH DE                      >> confirmed
  $339c: d9       EXX                          >> confirmed
  $339d: ed4b665c LD BC,(0x5c66)               >> confirmed
  $33a1: c9       RET                          >> confirmed
  $33a2: f1       POP AF                       >> confirmed
  $33a3: 3a675c   LD A,(0x5c67)                >> confirmed
  $33a6: d9       EXX                          >> confirmed
  $33a7: 18c3     JR 0x336c                    >> confirmed
  $33a9: d5       PUSH DE                      >> confirmed
  $33aa: e5       PUSH HL                      >> confirmed
  $33ab: 010500   LD BC,5                      >> confirmed
  $33ae: cd051f   CALL 0x1f05                  >> confirmed
  $33b1: e1       POP HL                       >> confirmed
  $33b2: d1       POP DE                       >> confirmed
  $33b3: c9       RET                          >> confirmed
  $33b4: ed5b655c LD DE,(0x5c65)               >> confirmed
  $33b8: cdc033   CALL 0x33c0                  >> confirmed
  $33bb: ed53655c LD (0x5c65),DE               >> confirmed
  $33bf: c9       RET                          >> confirmed
  $33c0: cda933   CALL 0x33a9                  >> confirmed
  $33c3: edb0     LDIR                         >> confirmed
  $33c5: c9       RET                          >> confirmed
  $33c6: c9       RET                          .. static only
  $33c7: 3e82     LD A,0x82                    .. static only
  $33c9: d3ff     OUT (0x00ff),A               .. static only
  $33cb: d9       EXX                          >> confirmed
  $33cc: e5       PUSH HL                      >> confirmed
  $33cd: d9       EXX                          >> confirmed
  $33ce: e3       EX (SP),HL                   >> confirmed
  $33cf: c5       PUSH BC                      >> confirmed
  $33d0: 7e       LD A,(HL)                    >> confirmed
  $33d1: e6c0     AND 0xc0                     >> confirmed
  $33d3: 07       RLCA                         >> confirmed
  $33d4: 07       RLCA                         >> confirmed
  $33d5: 4f       LD C,A                       >> confirmed
  $33d6: 0c       INC C                        >> confirmed
  $33d7: 7e       LD A,(HL)                    >> confirmed
  $33d8: e63f     AND 0x3f                     >> confirmed
  $33da: 2002     JR nz,0x33de                 >> confirmed
  $33dc: 23       INC HL                       >> confirmed
  $33dd: 7e       LD A,(HL)                    >> confirmed
  $33de: c650     ADD A,0x50                   >> confirmed
  $33e0: 12       LD (DE),A                    >> confirmed
  $33e1: 3e05     LD A,5                       >> confirmed
  $33e3: 91       SUB C                        >> confirmed
  $33e4: 23       INC HL                       >> confirmed
  $33e5: 13       INC DE                       >> confirmed
  $33e6: 0600     LD B,0                       >> confirmed
  $33e8: edb0     LDIR                         >> confirmed
  $33ea: c1       POP BC                       >> confirmed
  $33eb: e3       EX (SP),HL                   >> confirmed
  $33ec: d9       EXX                          >> confirmed
  $33ed: e1       POP HL                       >> confirmed
  $33ee: d9       EXX                          >> confirmed
  $33ef: 47       LD B,A                       >> confirmed
  $33f0: af       XOR A                        >> confirmed
  $33f1: 05       DEC B                        >> confirmed
  $33f2: c8       RET z                        >> confirmed
  $33f3: 12       LD (DE),A                    >> confirmed
  $33f4: 13       INC DE                       >> confirmed
  $33f5: 18fa     JR 0x33f1                    >> confirmed
  $33f7: a7       AND A                        >> confirmed
  $33f8: c8       RET z                        >> confirmed
  $33f9: f5       PUSH AF                      >> confirmed
  $33fa: d5       PUSH DE                      >> confirmed
  $33fb: 110000   LD DE,0                      >> confirmed
  $33fe: cdc833   CALL 0x33c8                  >> confirmed
  $3401: d1       POP DE                       >> confirmed
  $3402: f1       POP AF                       >> confirmed
  $3403: 3d       DEC A                        >> confirmed
  $3404: 18f2     JR 0x33f8                    >> confirmed
  $3406: 4f       LD C,A                       >> confirmed
  $3407: 07       RLCA                         >> confirmed
  $3408: 07       RLCA                         >> confirmed
  $3409: 81       ADD A,C                      >> confirmed
  $340a: 4f       LD C,A                       >> confirmed
  $340b: 0600     LD B,0                       >> confirmed
  $340d: 09       ADD HL,BC                    >> confirmed
  $340e: c9       RET                          >> confirmed
  $340f: d5       PUSH DE                      >> confirmed
  $3410: 2a685c   LD HL,(0x5c68)               >> confirmed
  $3413: cd0634   CALL 0x3406                  >> confirmed
  $3416: cdc033   CALL 0x33c0                  >> confirmed
  $3419: e1       POP HL                       >> confirmed
  $341a: c9       RET                          >> confirmed
  $341b: 62       LD H,D                       >> confirmed
  $341c: 6b       LD L,E                       >> confirmed
  $341d: d9       EXX                          >> confirmed
  $341e: e5       PUSH HL                      >> confirmed
  $341f: 21c532   LD HL,0x32c5                 >> confirmed
  $3422: d9       EXX                          >> confirmed
  $3423: cdf733   CALL 0x33f7                  >> confirmed
  $3426: cdc833   CALL 0x33c8                  >> confirmed
  $3429: d9       EXX                          >> confirmed
  $342a: e1       POP HL                       >> confirmed
  $342b: d9       EXX                          >> confirmed
  $342c: c9       RET                          >> confirmed
  $342d: e5       PUSH HL                      >> confirmed
  $342e: eb       EX DE,HL                     >> confirmed
  $342f: 2a685c   LD HL,(0x5c68)               >> confirmed
  $3432: cd0634   CALL 0x3406                  >> confirmed
  $3435: eb       EX DE,HL                     >> confirmed
  $3436: cdc033   CALL 0x33c0                  >> confirmed
  $3439: eb       EX DE,HL                     >> confirmed
  $343a: e1       POP HL                       >> confirmed
  $343b: c9       RET                          >> confirmed
  $343c: 0605     LD B,5                       >> confirmed
  $343e: 1a       LD A,(DE)                    >> confirmed
  $343f: 4e       LD C,(HL)                    >> confirmed
  $3440: eb       EX DE,HL                     >> confirmed
  $3441: 12       LD (DE),A                    >> confirmed
  $3442: 71       LD (HL),C                    >> confirmed
  $3443: 23       INC HL                       >> confirmed
  $3444: 13       INC DE                       >> confirmed
  $3445: 10f7     DJNZ 0x343e                  >> confirmed
  $3447: eb       EX DE,HL                     >> confirmed
  $3448: c9       RET                          >> confirmed
  $3449: 09       ADD HL,BC                    .. static only
  $344a: eb       EX DE,HL                     .. static only
  $344b: 189a     JR 0x33e7                    .. static only
  $344d: 7b       LD A,E                       .. static only
  $344e: d39f     OUT (0x009f),A               .. static only
  $3450: 7a       LD A,D                       .. static only
  $3451: e61f     AND 0x1f                     .. static only
  $3453: d3df     OUT (0x00df),A               .. static only
  $3455: 00       NOP                          .. static only
  $3456: dbbf     IN A,(0x00bf)                .. static only
  $3458: f5       PUSH AF                      .. static only
  $3459: af       XOR A                        .. static only
  $345a: d3df     OUT (0x00df),A               .. static only
  $345c: f1       POP AF                       .. static only
  $345d: c9       RET                          .. static only
IEC_OPEN_SEND_SHARED:
  $345e: 3a9d5c   LD A,(0x5c9d)                >> confirmed
  $3461: b7       OR A                         >> confirmed
  $3462: f8       RET m                        >> confirmed
  $3463: 21925c   LD HL,0x5c92                 >> confirmed
  $3466: 3600     LD (HL),0                    >> confirmed
  $3468: 3ab05c   LD A,(0x5cb0)                >> confirmed
  $346b: e603     AND 3                        >> confirmed
  $346d: c608     ADD A,8                      >> confirmed
  $346f: cd7830   CALL IEC_LISTEN_DEVNUM       >> confirmed
  $3472: 0600     LD B,0                       >> confirmed
  $3474: 7e       LD A,(HL)                    >> confirmed
  $3475: a7       AND A                        >> confirmed
  $3476: 280b     JR z,0x3483                  >> confirmed
  $3478: 50       LD D,B                       .. static only
  $3479: 33       INC SP                       .. static only
  $347a: 3a925c   LD A,(0x5c92)                >> confirmed
  $347d: b7       OR A                         >> confirmed
  $347e: fa3133   JP m,0x3331                  >> confirmed
  $3481: 3a9c5c   LD A,(0x5c9c)                >> confirmed
  $3484: e5       PUSH HL                      >> confirmed
  $3485: cd7f2d   CALL 0x2d7f                  >> confirmed
  $3488: e1       POP HL                       >> confirmed
  $3489: 78       LD A,B                       >> confirmed
  $348a: b1       OR C                         >> confirmed
  $348b: 2f       CPL                          >> confirmed
  $348c: 4f       LD C,A                       >> confirmed
  $348d: cd8e2d   CALL 0x2d8e                  >> confirmed
  $3490: d1       POP DE                       >> confirmed
  $3491: c9       RET                          >> confirmed
  $3492: f5       PUSH AF                      .. static only
  $3493: c36030   JP IEC_UNLISTEN              >> confirmed
  $3496: 3e61     LD A,0x61                    .. static only
  $3498: 329d5c   LD (0x5c9d),A                .. static only
  $349b: cd5e34   CALL IEC_OPEN_SEND_SHARED    .. static only
  $349e: cdc731   CALL 0x31c7                  .. static only
  $34a1: 3ab05c   LD A,(0x5cb0)                .. static only
  $34a4: e603     AND 3                        .. static only
  $34a6: c608     ADD A,8                      .. static only
  $34a8: cd7830   CALL IEC_LISTEN_DEVNUM       .. static only
  $34ab: 3e61     LD A,0x61                    .. static only
  $34ad: cd5033   CALL IEC_SEND_SECONDARY_RELEASE_ATN .. static only
  $34b0: dde5     PUSH IX                      .. static only
  $34b2: e1       POP HL                       .. static only
  $34b3: cd991e   CALL 0x1e99                  >> confirmed
  $34b6: 212b2d   LD HL,0x2d2b                 >> confirmed
  $34b9: e5       PUSH HL                      >> confirmed
  $34ba: c5       PUSH BC                      >> confirmed
  $34bb: c9       RET                          >> confirmed
  $34bc: 7e       LD A,(HL)                    .. static only
  $34bd: e5       PUSH HL                      .. static only
  $34be: cdbc30   CALL 0x30bc                  .. static only
  $34c1: 21925c   LD HL,0x5c92                 .. static only
  $34c4: cb7e     BIT 7,(HL)                   .. static only
  $34c6: 206f     JR nz,0x3537                 .. static only
  $34c8: e1       POP HL                       .. static only
  $34c9: 0606     LD B,6                       .. static only
  $34cb: 23       INC HL                       .. static only
  $34cc: c5       PUSH BC                      .. static only
  $34cd: e5       PUSH HL                      .. static only
  $34ce: 7e       LD A,(HL)                    .. static only
  $34cf: cdbc30   CALL 0x30bc                  .. static only
  $34d2: e1       POP HL                       .. static only
  $34d3: 23       INC HL                       .. static only
  $34d4: c1       POP BC                       .. static only
  $34d5: 10f5     DJNZ 0x34cc                  .. static only
  $34d7: a7       AND A                        .. static only
  $34d8: ed4b9a5c LD BC,(0x5c9a)               .. static only
  $34dc: 2a985c   LD HL,(0x5c98)               .. static only
  $34df: ed42     SBC HL,BC                    .. static only
  $34e1: 300f     JR nc,0x34f2                 .. static only
  $34e3: 2a985c   LD HL,(0x5c98)               .. static only
  $34e6: 7e       LD A,(HL)                    .. static only
  $34e7: e5       PUSH HL                      .. static only
  $34e8: cdbc30   CALL 0x30bc                  .. static only
  $34eb: 47       LD B,A                       >> confirmed
  $34ec: 7e       LD A,(HL)                    >> confirmed
  $34ed: 23       INC HL                       >> confirmed
  $34ee: b6       OR (HL)                      >> confirmed
  $34ef: 23       INC HL                       >> confirmed
  $34f0: b6       OR (HL)                      >> confirmed
  $34f1: 23       INC HL                       >> confirmed
  $34f2: cd6030   CALL IEC_UNLISTEN            >> confirmed
  $34f5: 3a9d5c   LD A,(0x5c9d)                >> confirmed
  $34f8: cb7f     BIT 7,A                      >> confirmed
  $34fa: c0       RET nz                       >> confirmed
  $34fb: 3ab05c   LD A,(0x5cb0)                >> confirmed
  $34fe: e603     AND 3                        >> confirmed
  $3500: c608     ADD A,8                      >> confirmed
  $3502: cd7830   CALL IEC_LISTEN_DEVNUM       >> confirmed
  $3505: 3a9d5c   LD A,(0x5c9d)                >> confirmed
  $3508: e6ef     AND 0xef                     >> confirmed
  $350a: f6e0     OR 0xe0                      >> confirmed
  $350c: cd5033   CALL IEC_SEND_SECONDARY_RELEASE_ATN >> confirmed
  $350f: c36030   JP IEC_UNLISTEN              >> confirmed
CATALOG_REOPEN_AND_PEEK7:
  $3512: 17       RLA                          >> confirmed
  $3513: 77       LD (HL),A                    >> confirmed
  $3514: 1f       RRA                          >> confirmed
CATALOG_OPEN_CH0_ENTRY:
  $3515: 3e60     LD A,0x60                    >> confirmed (live STEP, 2026-10-28 -- see note below)
  $3517: 329d5c   LD (0x5c9d),A                >> confirmed (live STEP, 2026-10-28)
  $351a: cd5e34   CALL IEC_OPEN_SEND_SHARED    >> confirmed (live STEP, 2026-10-28)
  ; NOTE (2026-10-28): this 7-byte block ($3515-$351c) previously read
  ; "23 77 23 77 e1 c9" (INC HL/LD(HL),A/INC HL/LD(HL),A/POP HL/RET) in
  ; this file, sourced from INSTRTRACE ground-truth logs (both the older
  ; accumulated ones AND a fresh regeneration from a same-day trace all
  ; agreed on that wrong reading). A direct live BREAK 3515 + STEP
  ; sequence (PC 3515->3517->351a->345e, AF/SP changing exactly as this
  ; corrected decode predicts, SP dropping by 2 confirming the CALL)
  ; settled it conclusively in favor of the decode shown here. The INSTRTRACE
  ; ground-truth mechanism producing a wrong, mutually-agreeing answer twice
  ; at this address is itself unexplained -- treat any address this
  ; surprising as worth a live BREAK+STEP re-check rather than fully
  ; trusting even repeated ">> confirmed" trace entries.
  $351d: 3ab05c   LD A,(0x5cb0)                >> confirmed
  $3520: e603     AND 3                        >> confirmed
  $3522: c608     ADD A,8                      >> confirmed
  $3524: cd7530   CALL IEC_SEND_DEVNUM         >> confirmed
  $3527: 3e60     LD A,0x60                    >> confirmed
  $3529: cd5d33   CALL 0x335d                  >> confirmed
  $352c: af       XOR A                        >> confirmed
  $352d: 32925c   LD (0x5c92),A                >> confirmed
  $3530: cd3831   CALL IEC_RECEIVE_BYTE        >> confirmed
  $3533: fef0     CP 0xf0                      >> confirmed
  $3535: 200b     JR nz,0x3542                 >> confirmed
  $3537: 3eff     LD A,0xff                    .. static only
  $3539: 32a05c   LD (0x5ca0),A                .. static only
  $353c: cd8133   CALL 0x3381                  .. static only
  $353f: c34133   JP 0x3341                    .. static only
  $3542: 0607     LD B,7                       >> confirmed
  $3544: 2a5f5c   LD HL,(0x5c5f)               >> confirmed
  $3547: c5       PUSH BC                      >> confirmed
  $3548: e5       PUSH HL                      >> confirmed
  $3549: cd3831   CALL IEC_RECEIVE_BYTE        >> confirmed
  $354c: e1       POP HL                       >> confirmed
CATALOG_PEEK7_STORE_BYTE:
  $354d: 77       LD (HL),A                    >> confirmed
  $354e: 23       INC HL                       >> confirmed
  $354f: c1       POP BC                       >> confirmed
  $3550: 10f5     DJNZ 0x3547                  >> confirmed
CATALOG_REOPEN_RET:
  $3552: c9       RET                          >> confirmed
  $3553: cd9f31   CALL 0x319f                  .. static only
  $3556: 2a9a5c   LD HL,(0x5c9a)               .. static only
  $3559: e5       PUSH HL                      .. static only
  $355a: cd3831   CALL IEC_RECEIVE_BYTE        .. static only
  $355d: e1       POP HL                       .. static only
  $355e: f5       PUSH AF                      .. static only
  $355f: 3a935c   LD A,(0x5c93)                .. static only
  $3562: a7       AND A                        .. static only
  $3563: 2022     JR nz,0x3587                 .. static only
  $3565: f1       POP AF                       .. static only
  $3566: 77       LD (HL),A                    .. static only
  $3567: 23       INC HL                       .. static only
  $3568: 3a925c   LD A,(0x5c92)                .. static only
  $356b: cb77     BIT 6,A                      .. static only
  $356d: 28ea     JR z,0x3559                  .. static only
  $356f: cd5430   CALL 0x3054                  .. static only
  $3572: cdf234   CALL 0x34f2                  .. static only
  $3575: da2e33   JP c,0x332e                  .. static only
  $3578: 3e4f     LD A,0x4f                    .. static only
  $357a: cd2b3e   CALL 0x3e2b                  .. static only
  $357d: 3e4b     LD A,0x4b                    .. static only
  $357f: cd2b3e   CALL 0x3e2b                  .. static only
  $3582: 3e0d     LD A,13                      .. static only
  $3584: c32b3e   JP 0x3e2b                    .. static only
  $3587: f1       POP AF                       .. static only
  $3588: be       CP (HL)                      .. static only
  $3589: 2809     JR z,0x3594                  .. static only
  $358b: cd5430   CALL 0x3054                  .. static only
  $358e: cdf234   CALL 0x34f2                  .. static only
  $3591: c32b33   JP 0x332b                    .. static only
  $3594: 23       INC HL                       .. static only
  $3595: 3a925c   LD A,(0x5c92)                .. static only
  $3598: cb77     BIT 6,A                      .. static only
  $359a: 28bd     JR z,0x3559                  .. static only
  $359c: 18d4     JR 0x3572                    .. static only
TYPE_CHECK_CP:
  $359e: dd7ef9   LD A,(IX-7)                  >> confirmed
  $35a1: ddbe00   CP (IX)                      >> confirmed
  $35a4: c8       RET z                        >> confirmed
TYPE_CHECK_FAIL_B9:
  $35a5: 0609     LD B,9                       >> confirmed
  $35a7: c33933   JP ERR_DISPATCH              >> confirmed
  $35aa: 53       LD D,E                       .. static only
  $35ab: c1       POP BC                       .. static only
  $35ac: d6c5     SUB 0xc5                     .. static only
  $35ae: 80       ADD A,B                      .. static only
  $35af: c7       RST 0                        .. static only
  $35b0: 3c       INC A                        .. static only
  $35b1: 4c       LD C,H                       .. static only
  $35b2: cf       RST 8                        .. static only
  $35b3: c1       POP BC                       .. static only
  $35b4: c480ca   CALL nz,0xca80               .. static only
  $35b7: 3c       INC A                        .. static only
  $35b8: 56       LD D,(HL)                    .. static only
  $35b9: 45       LD B,L                       .. static only
  $35ba: d2c9c6   JP nc,0xc6c9                 .. static only
  $35bd: d9       EXX                          .. static only
  $35be: 80       ADD A,B                      .. static only
  $35bf: 2a655c   LD HL,(0x5c65)               >> confirmed
  $35c2: 11fbff   LD DE,0xfffb                 >> confirmed
  $35c5: e5       PUSH HL                      >> confirmed
  $35c6: 19       ADD HL,DE                    >> confirmed
  $35c7: d1       POP DE                       >> confirmed
  $35c8: c9       RET                          >> confirmed
  $35c9: cf       RST 8                        .. static only
  $35ca: 80       ADD A,B                      .. static only
  $35cb: fe3c     CP 0x3c                      .. static only
  $35cd: 49       LD C,C                       .. static only
  $35ce: cec9     ADC A,0xc9                   .. static only
  $35d0: d4c9c1   CALL nc,0xc1c9               .. static only
  $35d3: ccc9da   CALL z,0xdac9                .. static only
  $35d6: c5       PUSH BC                      .. static only
  $35d7: 80       ADD A,B                      .. static only
  $35d8: 33       INC SP                       .. static only
  $35d9: 3d       DEC A                        .. static only
  $35da: 56       LD D,(HL)                    .. static only
  $35db: 41       LD B,C                       .. static only
  $35dc: ccc9c4   CALL z,0xc4c9                .. static only
  $35df: c1       POP BC                       .. static only
  $35e0: d4c580   CALL nc,0x80c5               .. static only
  $35e3: 2f       CPL                          .. static only
  $35e4: 3d       DEC A                        .. static only
  $35e5: d0       RET nc                       .. static only
  $35e6: 80       ADD A,B                      .. static only
  $35e7: 1c       INC E                        .. static only
  $35e8: 3d       DEC A                        .. static only
  $35e9: d1       POP DE                       .. static only
  $35ea: 80       ADD A,B                      .. static only
  $35eb: 23       INC HL                       .. static only
  $35ec: 3d       DEC A                        .. static only
  $35ed: 52       LD D,D                       .. static only
  $35ee: c5       PUSH BC                      .. static only
  $35ef: cec1     ADC A,0xc1                   .. static only
  $35f1: cdc580   CALL 0x80c5                  .. static only
  $35f4: 27       DAA                          .. static only
  $35f5: 3d       DEC A                        .. static only
  $35f6: d2802b   JP nc,0x2b80                 .. static only
  $35f9: 3d       DEC A                        .. static only
  $35fa: 45       LD B,L                       .. static only
  $35fb: 50       LD D,B                       .. static only
  $35fc: 52       LD D,D                       .. static only
  $35fd: 4f       LD C,A                       .. static only
  $35fe: 4d       LD C,L                       .. static only
  $35ff: 80       ADD A,B                      .. static only
  $3600: 68       LD L,B                       .. static only
  $3601: 3e45     LD A,0x45                    .. static only
  $3603: 52       LD D,D                       .. static only
  $3604: 52       LD D,D                       .. static only
  $3605: 4f       LD C,A                       .. static only
  $3606: 52       LD D,D                       .. static only
  $3607: 80       ADD A,B                      .. static only
  $3608: 62       LD H,D                       .. static only
  $3609: 3e43     LD A,0x43                    .. static only
  $360b: 4f       LD C,A                       .. static only
  $360c: cdcdc1   CALL 0xc1cd                  .. static only
  $360f: cec4     ADC A,0xc4                   .. static only
  $3611: 80       ADD A,B                      .. static only
  $3612: 4d       LD C,L                       .. static only
  $3613: 3d       DEC A                        .. static only
  $3614: 43       LD B,E                       .. static only
  $3615: c1       POP BC                       .. static only
  $3616: d480fe   CALL nc,0xfe80               .. static only
  $3619: 3c       INC A                        .. static only
  $361a: 44       LD B,H                       .. static only
  $361b: 52       LD D,D                       .. static only
  $361c: c9       RET                          .. static only
  $361d: d6c5     SUB 0xc5                     .. static only
  $361f: 80       ADD A,B                      .. static only
  $3620: da3eff   JP c,0xff3e                  .. static only
  $3623: e5       PUSH HL                      >> confirmed
  $3624: 32745c   LD (0x5c74),A                >> confirmed
  $3627: cd713e   CALL 0x3e71                  >> confirmed
  $362a: c3793d   JP 0x3d79                    >> confirmed
  $362d: 2805     JR z,0x3634                  >> confirmed
  $362f: 3efe     LD A,0xfe                    >> confirmed
  $3631: c37f3d   JP 0x3d7f                    >> confirmed
  $3634: c3853d   JP 0x3d85                    >> confirmed
  $3637: 2848     JR z,0x3681                  >> confirmed
  $3639: 010700   LD BC,7                      >> confirmed
  $363c: 3a745c   LD A,(0x5c74)                >> confirmed
  $363f: a7       AND A                        >> confirmed
  $3640: 2802     JR z,0x3644                  >> confirmed
  $3642: 0e0e     LD C,14                      >> confirmed
  $3644: c38e3d   JP 0x3d8e                    >> confirmed
  $3647: d5       PUSH DE                      >> confirmed
  $3648: dde1     POP IX                       >> confirmed
  $364a: 0607     LD B,7                       >> confirmed
  $364c: 3e20     LD A,0x20                    >> confirmed
  $364e: 12       LD (DE),A                    >> confirmed
  $364f: 13       INC DE                       >> confirmed
  $3650: 10fc     DJNZ 0x364e                  >> confirmed
  $3652: c3923d   JP 0x3d92                    >> confirmed
  $3655: ed539e5c LD (0x5c9e),DE               >> confirmed
  $3659: 79       LD A,C                       >> confirmed
  $365a: 329c5c   LD (0x5c9c),A                >> confirmed
  $365d: 21eeff   LD HL,0xffee                 >> confirmed
  $3660: 1a       LD A,(DE)                    >> confirmed
  $3661: fe40     CP 0x40                      >> confirmed
  $3663: cc7936   CALL z,0x3679                >> confirmed
  $3666: 2803     JR z,0x366b                  >> confirmed
  $3668: 21f0ff   LD HL,0xfff0                 >> confirmed
  $366b: 0b       DEC BC                       >> confirmed
  $366c: 09       ADD HL,BC                    >> confirmed
  $366d: 03       INC BC                       >> confirmed
  $366e: 3011     JR nc,0x3681                 >> confirmed
  $3670: cdf234   CALL 0x34f2                  .. static only
  $3673: cd523c   CALL 0x3c52                  .. static only
  $3676: c34206   JP 0x0642                    .. static only
  $3679: 13       INC DE                       .. static only
  $367a: 1a       LD A,(DE)                    .. static only
  $367b: 1b       DEC DE                       .. static only
  $367c: fe3a     CP 0x3a                      .. static only
  $367e: 20f0     JR nz,0x3670                 .. static only
  $3680: c9       RET                          .. static only
  $3681: c3983d   JP 0x3d98                    >> confirmed
  $3684: 2813     JR z,0x3699                  >> confirmed
  $3686: d9       EXX                          >> confirmed
  $3687: 5e       LD E,(HL)                    >> confirmed
  $3688: 7b       LD A,E                       >> confirmed
  $3689: 17       RLA                          >> confirmed
  $368a: 9f       SBC A,A                      >> confirmed
  $368b: 57       LD D,A                       >> confirmed
  $368c: 19       ADD HL,DE                    >> confirmed
  $368d: d9       EXX                          >> confirmed
  $368e: c9       RET                          >> confirmed
  $368f: 13       INC DE                       >> confirmed
  $3690: 13       INC DE                       >> confirmed
  $3691: 1a       LD A,(DE)                    >> confirmed
  $3692: 1b       DEC DE                       >> confirmed
  $3693: 1b       DEC DE                       >> confirmed
  $3694: a7       AND A                        >> confirmed
  $3695: 20ef     JR nz,0x3686                 >> confirmed
  $3697: d9       EXX                          >> confirmed
  $3698: 23       INC HL                       >> confirmed
  $3699: d9       EXX                          >> confirmed
  $369a: c9       RET                          >> confirmed
  $369b: f1       POP AF                       >> confirmed
  $369c: d9       EXX                          >> confirmed
  $369d: e3       EX (SP),HL                   >> confirmed
  $369e: d9       EXX                          >> confirmed
  $369f: c9       RET                          >> confirmed
  $36a0: 3a745c   LD A,(0x5c74)                .. static only
  $36a3: fe03     CP 3                         .. static only
  $36a5: ca8a1c   JP z,0x1c8a                  .. static only
  $36a8: c3a23d   JP 0x3da2                    .. static only
  $36ab: cbf9     SET 7,C                      .. static only
  $36ad: 300c     JR nc,0x36bb                 .. static only
  $36af: ef       RST 0x28                     >> confirmed
  $36b0: 00       NOP                          .. static only
  $36b1: 00       NOP                          .. static only
  $36b2: 3a745c   LD A,(0x5c74)                .. static only
  $36b5: 3d       DEC A                        .. static only
  $36b6: c9       RET                          >> confirmed
  $36b7: 16c3     LD D,0xc3                    .. static only
  $36b9: 70       LD (HL),B                    .. static only
  $36ba: 06c2     LD B,0xc2                    .. static only
  $36bc: 8a       ADC A,D                      .. static only
  $36bd: 1c       INC E                        .. static only
  $36be: c3a93d   JP 0x3da9                    .. static only
  $36c1: 2818     JR z,0x36db                  .. static only
  $36c3: 23       INC HL                       .. static only
  $36c4: 7e       LD A,(HL)                    .. static only
  $36c5: dd7701   LD (IX+1),A                  .. static only
  $36c8: 23       INC HL                       .. static only
  $36c9: 7e       LD A,(HL)                    .. static only
  $36ca: dd7702   LD (IX+2),A                  .. static only
  $36cd: 23       INC HL                       .. static only
  $36ce: dd7103   LD (IX+3),C                  .. static only
  $36d1: 3e01     LD A,1                       .. static only
  $36d3: cb71     BIT 6,C                      .. static only
  $36d5: 2801     JR z,0x36d8                  .. static only
  $36d7: 3c       INC A                        .. static only
  $36d8: dd7700   LD (IX),A                    .. static only
  $36db: eb       EX DE,HL                     .. static only
  $36dc: c3af3d   JP 0x3daf                    .. static only
  $36df: fe29     CP 0x29                      .. static only
  $36e1: 20d8     JR nz,0x36bb                 .. static only
  $36e3: c3b33d   JP 0x3db3                    .. static only
  $36e6: eb       EX DE,HL                     .. static only
  $36e7: c38437   JP 0x3784                    .. static only
  $36ea: feaa     CP 0xaa                      >> confirmed
  $36ec: 201e     JR nz,0x370c                 >> confirmed
  $36ee: 3a745c   LD A,(0x5c74)                .. static only
  $36f1: fe03     CP 3                         .. static only
  $36f3: ca8a1c   JP z,0x1c8a                  .. static only
  $36f6: c3ba3d   JP 0x3dba                    .. static only
  $36f9: dd360100 LD (IX+1),0                  .. static only
  $36fd: dd36021b LD (IX+2),0x1b               .. static only
  $3701: 210040   LD HL,0x4000                 .. static only
  $3704: dd7503   LD (IX+3),L                  .. static only
  $3707: dd7404   LD (IX+4),H                  .. static only
  $370a: 1835     JR 0x3741                    .. static only
  $370c: feaf     CP 0xaf                      >> confirmed
  $370e: 2037     JR nz,0x3747                 >> confirmed
  $3710: 3a745c   LD A,(0x5c74)                .. static only
  $3713: fe03     CP 3                         .. static only
  $3715: ca8a1c   JP z,0x1c8a                  .. static only
  $3718: c3c13d   JP 0x3dc1                    .. static only
  $371b: 200a     JR nz,0x3727                 .. static only
  $371d: 3a745c   LD A,(0x5c74)                .. static only
  $3720: a7       AND A                        .. static only
  $3721: ca8a1c   JP z,0x1c8a                  .. static only
  $3724: c3c83d   JP 0x3dc8                    .. static only
  $3727: c3cd3d   JP 0x3dcd                    .. static only
  $372a: fe2c     CP 0x2c                      .. static only
  $372c: cada3d   JP z,0x3dda                  .. static only
  $372f: 3a745c   LD A,(0x5c74)                .. static only
  $3732: a7       AND A                        .. static only
  $3733: ca8a1c   JP z,0x1c8a                  .. static only
  $3736: c3d43d   JP 0x3dd4                    .. static only
  $3739: dd7103   LD (IX+3),C                  .. static only
  $373c: dd7004   LD (IX+4),B                  .. static only
  $373f: 60       LD H,B                       .. static only
  $3740: 69       LD L,C                       .. static only
  $3741: dd360003 LD (IX),3                    .. static only
  $3745: 183d     JR 0x3784                    .. static only
  $3747: feca     CP 0xca                      >> confirmed
  $3749: 2809     JR z,0x3754                  >> confirmed
  $374b: c3f03d   JP 0x3df0                    >> confirmed
  $374e: dd360480 LD (IX+4),0x80               >> confirmed
  $3752: 1810     JR TYPE_CHECK_FIELD_ZERO     >> confirmed
  $3754: 3a745c   LD A,(0x5c74)                .. static only
  $3757: a7       AND A                        .. static only
  $3758: c28a1c   JP nz,0x1c8a                 .. static only
  $375b: c3f63d   JP 0x3df6                    .. static only
  $375e: dd7103   LD (IX+3),C                  .. static only
  $3761: dd7004   LD (IX+4),B                  .. static only
TYPE_CHECK_FIELD_ZERO:
  $3764: dd360000 LD (IX),0                    >> confirmed
BLOCKFREE_SBC_CALC:
  $3768: 2a595c   LD HL,(0x5c59)               >> confirmed
  $376b: ed5b535c LD DE,(0x5c53)               >> confirmed
  $376f: 37       SCF                          >> confirmed
  $3770: ed52     SBC HL,DE                    >> confirmed
  $3772: dd7501   LD (IX+1),L                  >> confirmed
  $3775: dd7402   LD (IX+2),H                  >> confirmed
  $3778: 2a4b5c   LD HL,(0x5c4b)               >> confirmed
  $377b: ed52     SBC HL,DE                    >> confirmed
  $377d: dd7505   LD (IX+5),L                  >> confirmed
  $3780: dd7406   LD (IX+6),H                  >> confirmed
  $3783: eb       EX DE,HL                     >> confirmed
  $3784: 3a745c   LD A,(0x5c74)                >> confirmed
  $3787: a7       AND A                        >> confirmed
  $3788: cace38   JP z,0x38ce                  >> confirmed
  $378b: e5       PUSH HL                      >> confirmed
  $378c: 010700   LD BC,7                      >> confirmed
  $378f: dd09     ADD IX,BC                    >> confirmed
RECORD_PTR_ADVANCE_STORE:
  $3791: dd225f5c LD (0x5c5f),IX               >> confirmed
CALL_CATALOG_REOPEN:
  $3795: cd1235   CALL CATALOG_REOPEN_AND_PEEK7 >> confirmed
LOAD_A_FE_BEFORE_TRAMP:
  $3798: 3efe     LD A,0xfe                    >> confirmed
JP_TRAMP_3e03:
  $379a: c3033e   JP TRAMP_CALL1601_TO_379D    >> confirmed
CALL_TYPE_CHECK:
  $379d: cd9e35   CALL TYPE_CHECK_CP           >> confirmed
  $37a0: fd365203 LD (IY+0x52),3               .. static only
  $37a4: e1       POP HL                       .. static only
  $37a5: dd7e00   LD A,(IX)                    .. static only
  $37a8: fe03     CP 3                         .. static only
  $37aa: 280c     JR z,0x37b8                  .. static only
  $37ac: 3a745c   LD A,(0x5c74)                .. static only
  $37af: 3d       DEC A                        .. static only
  $37b0: cafa37   JP z,0x37fa                  .. static only
  $37b3: fe02     CP 2                         .. static only
  $37b5: caac38   JP z,0x38ac                  .. static only
  $37b8: e5       PUSH HL                      .. static only
  $37b9: dd6efa   LD L,(IX-6)                  .. static only
  $37bc: dd66fb   LD H,(IX-5)                  .. static only
  $37bf: dd5e01   LD E,(IX+1)                  .. static only
  $37c2: dd5602   LD D,(IX+2)                  .. static only
  $37c5: 7c       LD A,H                       .. static only
  $37c6: b5       OR L                         .. static only
  $37c7: 2813     JR z,0x37dc                  .. static only
  $37c9: ed52     SBC HL,DE                    .. static only
  $37cb: 3005     JR nc,0x37d2                 .. static only
  $37cd: 060a     LD B,10                      .. static only
  $37cf: c33933   JP ERR_DISPATCH              .. static only
  $37d2: 2808     JR z,0x37dc                  .. static only
  $37d4: dd7e00   LD A,(IX)                    .. static only
  $37d7: fe03     CP 3                         .. static only
  $37d9: c2cd37   JP nz,0x37cd                 .. static only
  $37dc: e1       POP HL                       .. static only
  $37dd: 7c       LD A,H                       .. static only
  $37de: b5       OR L                         .. static only
  $37df: 2006     JR nz,0x37e7                 .. static only
  $37e1: dd6e03   LD L,(IX+3)                  .. static only
  $37e4: dd6604   LD H,(IX+4)                  .. static only
  $37e7: 229a5c   LD (0x5c9a),HL               .. static only
  $37ea: 3a745c   LD A,(0x5c74)                .. static only
  $37ed: fe02     CP 2                         .. static only
  $37ef: 3e00     LD A,0                       .. static only
  $37f1: 2001     JR nz,0x37f4                 .. static only
  $37f3: 3d       DEC A                        .. static only
  $37f4: 32935c   LD (0x5c93),A                .. static only
  $37f7: c35335   JP 0x3553                    .. static only
  $37fa: dd5e01   LD E,(IX+1)                  .. static only
  $37fd: dd5602   LD D,(IX+2)                  .. static only
  $3800: e5       PUSH HL                      .. static only
  $3801: 7c       LD A,H                       .. static only
  $3802: b5       OR L                         .. static only
  $3803: 2006     JR nz,0x380b                 .. static only
  $3805: 13       INC DE                       .. static only
  $3806: 13       INC DE                       .. static only
  $3807: 13       INC DE                       .. static only
  $3808: eb       EX DE,HL                     .. static only
  $3809: 180c     JR 0x3817                    .. static only
  $380b: dd6efa   LD L,(IX-6)                  .. static only
  $380e: dd66fb   LD H,(IX-5)                  .. static only
  $3811: eb       EX DE,HL                     .. static only
  $3812: 37       SCF                          .. static only
  $3813: ed52     SBC HL,DE                    .. static only
  $3815: 3809     JR c,0x3820                  .. static only
  $3817: 110500   LD DE,5                      .. static only
  $381a: 19       ADD HL,DE                    .. static only
  $381b: 44       LD B,H                       .. static only
  $381c: 4d       LD C,L                       .. static only
  $381d: c3093e   JP 0x3e09                    .. static only
  $3820: e1       POP HL                       .. static only
  $3821: dd7e00   LD A,(IX)                    .. static only
  $3824: a7       AND A                        .. static only
  $3825: 283f     JR z,0x3866                  .. static only
  $3827: 7c       LD A,H                       .. static only
  $3828: b5       OR L                         .. static only
  $3829: 2813     JR z,0x383e                  .. static only
  $382b: 2b       DEC HL                       .. static only
  $382c: 46       LD B,(HL)                    .. static only
  $382d: 2b       DEC HL                       .. static only
  $382e: 4e       LD C,(HL)                    .. static only
  $382f: 2b       DEC HL                       .. static only
  $3830: 03       INC BC                       .. static only
  $3831: 03       INC BC                       .. static only
  $3832: 03       INC BC                       .. static only
  $3833: dd225f5c LD (0x5c5f),IX               .. static only
  $3837: c30f3e   JP 0x3e0f                    .. static only
  $383a: dd2a5f5c LD IX,(0x5c5f)               .. static only
  $383e: 2a595c   LD HL,(0x5c59)               .. static only
  $3841: 2b       DEC HL                       .. static only
  $3842: dd4e01   LD C,(IX+1)                  .. static only
  $3845: dd4602   LD B,(IX+2)                  .. static only
  $3848: c5       PUSH BC                      .. static only
  $3849: 03       INC BC                       .. static only
  $384a: 03       INC BC                       .. static only
  $384b: 03       INC BC                       .. static only
  $384c: dd7efc   LD A,(IX-4)                  .. static only
  $384f: f5       PUSH AF                      .. static only
  $3850: c3153e   JP 0x3e15                    .. static only
  $3853: 23       INC HL                       .. static only
  $3854: f1       POP AF                       .. static only
  $3855: 77       LD (HL),A                    .. static only
  $3856: d1       POP DE                       .. static only
  $3857: 23       INC HL                       .. static only
  $3858: 73       LD (HL),E                    .. static only
  $3859: 23       INC HL                       .. static only
  $385a: 72       LD (HL),D                    .. static only
  $385b: 23       INC HL                       .. static only
  $385c: 229a5c   LD (0x5c9a),HL               .. static only
  $385f: af       XOR A                        .. static only
  $3860: 32935c   LD (0x5c93),A                .. static only
  $3863: c35335   JP 0x3553                    .. static only
  $3866: eb       EX DE,HL                     .. static only
  $3867: 2a595c   LD HL,(0x5c59)               .. static only
  $386a: 2b       DEC HL                       .. static only
  $386b: dd225f5c LD (0x5c5f),IX               .. static only
  $386f: dd4e01   LD C,(IX+1)                  .. static only
  $3872: dd4602   LD B,(IX+2)                  .. static only
  $3875: c5       PUSH BC                      .. static only
  $3876: c31b3e   JP 0x3e1b                    .. static only
  $3879: c1       POP BC                       .. static only
  $387a: e5       PUSH HL                      .. static only
  $387b: c5       PUSH BC                      .. static only
  $387c: c3213e   JP 0x3e21                    .. static only
  $387f: dd2a5f5c LD IX,(0x5c5f)               .. static only
  $3883: 23       INC HL                       .. static only
  $3884: dd4e05   LD C,(IX+5)                  .. static only
  $3887: dd4606   LD B,(IX+6)                  .. static only
  $388a: 09       ADD HL,BC                    .. static only
  $388b: 224b5c   LD (0x5c4b),HL               .. static only
  $388e: dd6604   LD H,(IX+4)                  .. static only
  $3891: 7c       LD A,H                       .. static only
  $3892: e6c0     AND 0xc0                     .. static only
  $3894: 200a     JR nz,0x38a0                 .. static only
  $3896: dd6e03   LD L,(IX+3)                  .. static only
  $3899: 22425c   LD (0x5c42),HL               .. static only
  $389c: fd360a00 LD (IY+10),0                 .. static only
  $38a0: d1       POP DE                       .. static only
  $38a1: e1       POP HL                       .. static only
  $38a2: 229a5c   LD (0x5c9a),HL               .. static only
  $38a5: af       XOR A                        .. static only
  $38a6: 32935c   LD (0x5c93),A                .. static only
  $38a9: c35335   JP 0x3553                    .. static only
  $38ac: dd4e01   LD C,(IX+1)                  .. static only
  $38af: dd4602   LD B,(IX+2)                  .. static only
  $38b2: c5       PUSH BC                      .. static only
  $38b3: 03       INC BC                       .. static only
  $38b4: c3273e   JP 0x3e27                    .. static only
  $38b7: 3680     LD (HL),0x80                 .. static only
  $38b9: eb       EX DE,HL                     .. static only
  $38ba: d1       POP DE                       .. static only
  $38bb: e5       PUSH HL                      .. static only
  $38bc: 229a5c   LD (0x5c9a),HL               .. static only
  $38bf: af       XOR A                        .. static only
  $38c0: 32935c   LD (0x5c93),A                .. static only
  $38c3: cd5335   CALL 0x3553                  .. static only
  $38c6: 0608     LD B,8                       .. static only
  $38c8: cda931   CALL PRINT_MSG_SKIP_LOOP     .. static only
  $38cb: c3cd08   JP 0x08cd                    .. static only
  $38ce: 22985c   LD (0x5c98),HL               .. static only
  $38d1: dd5e01   LD E,(IX+1)                  .. static only
  $38d4: dd5602   LD D,(IX+2)                  .. static only
  $38d7: 19       ADD HL,DE                    .. static only
  $38d8: 229a5c   LD (0x5c9a),HL               .. static only
  $38db: c39634   JP 0x3496                    .. static only
  $38de: fdcb0286 RES 0,(IY+2)                 >> confirmed
  $38e2: 3e0d     LD A,13                      >> confirmed
  $38e4: cd2b3e   CALL 0x3e2b                  >> confirmed
  $38e7: 010800   LD BC,8                      >> confirmed
  $38ea: c3383e   JP 0x3e38                    >> confirmed
  $38ed: 3e24     LD A,0x24                    >> confirmed
  $38ef: 12       LD (DE),A                    >> confirmed
  $38f0: ed539e5c LD (0x5c9e),DE               >> confirmed
  $38f4: af       XOR A                        >> confirmed
  $38f5: 32935c   LD (0x5c93),A                >> confirmed
  $38f8: 3c       INC A                        >> confirmed
  $38f9: 329c5c   LD (0x5c9c),A                >> confirmed
  $38fc: 13       INC DE                       >> confirmed
  $38fd: ed535f5c LD (0x5c5f),DE               >> confirmed
  $3901: cd1535   CALL 0x3515                  >> confirmed
  $3904: 0604     LD B,4                       >> confirmed
  $3906: 3e20     LD A,0x20                    >> confirmed
  $3908: cd2b3e   CALL 0x3e2b                  >> confirmed
  $390b: 10f9     DJNZ 0x3906                  >> confirmed
  $390d: 3e14     LD A,0x14                    >> confirmed
  $390f: cd2b3e   CALL 0x3e2b                  >> confirmed
  $3912: 3e01     LD A,1                       >> confirmed
  $3914: cd2b3e   CALL 0x3e2b                  >> confirmed
  $3917: 3e22     LD A,0x22                    >> confirmed
  $3919: cd2b3e   CALL 0x3e2b                  >> confirmed
  $391c: cd3831   CALL IEC_RECEIVE_BYTE        >> confirmed
  $391f: f5       PUSH AF                      >> confirmed
  $3920: cd2b3e   CALL 0x3e2b                  >> confirmed
  $3923: f1       POP AF                       >> confirmed
  $3924: fe22     CP 0x22                      >> confirmed
  $3926: 20f4     JR nz,0x391c                 >> confirmed
  $3928: 3e14     LD A,0x14                    >> confirmed
  $392a: cd2b3e   CALL 0x3e2b                  >> confirmed
  $392d: af       XOR A                        >> confirmed
  $392e: cd2b3e   CALL 0x3e2b                  >> confirmed
  $3931: cd3831   CALL IEC_RECEIVE_BYTE        >> confirmed
  $3934: a7       AND A                        >> confirmed
  $3935: 2805     JR z,0x393c                  >> confirmed
  $3937: cd2b3e   CALL 0x3e2b                  >> confirmed
  $393a: 18f5     JR 0x3931                    >> confirmed
  $393c: 3e0d     LD A,13                      >> confirmed
  $393e: cd2b3e   CALL 0x3e2b                  >> confirmed
  $3941: cd3831   CALL IEC_RECEIVE_BYTE        >> confirmed
  $3944: cd3831   CALL IEC_RECEIVE_BYTE        >> confirmed
  $3947: cd3831   CALL IEC_RECEIVE_BYTE        >> confirmed
  $394a: 4f       LD C,A                       >> confirmed
  $394b: c5       PUSH BC                      >> confirmed
  $394c: cd3831   CALL IEC_RECEIVE_BYTE        >> confirmed
  $394f: c1       POP BC                       >> confirmed
  $3950: 47       LD B,A                       >> confirmed
  $3951: c33c3e   JP 0x3e3c                    >> confirmed
  $3954: cd3831   CALL IEC_RECEIVE_BYTE        >> confirmed
  $3957: fe42     CP 0x42                      >> confirmed
  $3959: 20dc     JR nz,0x3937                 >> confirmed
  $395b: 3e20     LD A,0x20                    >> confirmed
  $395d: cd2b3e   CALL 0x3e2b                  >> confirmed
  $3960: 3e42     LD A,0x42                    >> confirmed
  $3962: cd2b3e   CALL 0x3e2b                  >> confirmed
  $3965: 060b     LD B,11                      >> confirmed
  $3967: c5       PUSH BC                      >> confirmed
  $3968: cd3831   CALL IEC_RECEIVE_BYTE        >> confirmed
  $396b: cd2b3e   CALL 0x3e2b                  >> confirmed
  $396e: c1       POP BC                       >> confirmed
  $396f: 10f6     DJNZ 0x3967                  >> confirmed
  $3971: 3e0d     LD A,13                      >> confirmed
  $3973: cd2b3e   CALL 0x3e2b                  >> confirmed
  $3976: c3f234   JP 0x34f2                    >> confirmed
  $3979: c5       PUSH BC                      .. static only
  $397a: 0c       INC C                        .. static only
  $397b: 0c       INC C                        .. static only
  $397c: 0c       INC C                        .. static only
  $397d: 79       LD A,C                       .. static only
  $397e: 329c5c   LD (0x5c9c),A                .. static only
  $3981: d5       PUSH DE                      .. static only
  $3982: c35e3e   JP 0x3e5e                    .. static only
  $3985: ed539e5c LD (0x5c9e),DE               .. static only
  $3989: 3a925c   LD A,(0x5c92)                .. static only
  $398c: 12       LD (DE),A                    .. static only
  $398d: 13       INC DE                       .. static only
  $398e: 3e3a     LD A,0x3a                    .. static only
  $3990: 12       LD (DE),A                    .. static only
  $3991: 13       INC DE                       .. static only
  $3992: e1       POP HL                       .. static only
  $3993: c1       POP BC                       .. static only
  $3994: edb0     LDIR                         .. static only
LOAD_OPEN_SHARED_CONT:
  $3996: 3e0d     LD A,13                      >> confirmed
  $3998: 12       LD (DE),A                    >> confirmed
  $3999: 3e98     LD A,0x98                    >> confirmed
  $399b: d3ff     OUT (0x00ff),A               >> confirmed
  $399d: 3e6f     LD A,0x6f                    >> confirmed
  $399f: 329d5c   LD (0x5c9d),A                >> confirmed
  $39a2: c35e34   JP IEC_OPEN_SEND_SHARED      >> confirmed
  $39a5: 21aa35   LD HL,0x35aa                 >> confirmed
  $39a8: cdb03c   CALL 0x3cb0                  >> confirmed
  $39ab: be       CP (HL)                      >> confirmed
  $39ac: 2818     JR z,0x39c6                  >> confirmed
  $39ae: 23       INC HL                       >> confirmed
  $39af: 7e       LD A,(HL)                    >> confirmed
  $39b0: fe80     CP 0x80                      >> confirmed
  $39b2: 20fa     JR nz,0x39ae                 >> confirmed
  $39b4: 23       INC HL                       >> confirmed
  $39b5: 23       INC HL                       >> confirmed
  $39b6: 23       INC HL                       >> confirmed
  $39b7: 7e       LD A,(HL)                    >> confirmed
  $39b8: 3c       INC A                        >> confirmed
  $39b9: ca763c   JP z,0x3c76                  >> confirmed
  $39bc: ed5b985c LD DE,(0x5c98)               >> confirmed
  $39c0: ed535d5c LD (0x5c5d),DE               >> confirmed
  $39c4: 18e2     JR 0x39a8                    >> confirmed
  $39c6: 17       RLA                          >> confirmed
  $39c7: 381e     JR c,0x39e7                  >> confirmed
  $39c9: 23       INC HL                       >> confirmed
  $39ca: cdb73c   CALL 0x3cb7                  >> confirmed
  $39cd: be       CP (HL)                      >> confirmed
  $39ce: 28f9     JR z,0x39c9                  >> confirmed
  $39d0: f680     OR 0x80                      >> confirmed
  $39d2: be       CP (HL)                      >> confirmed
  $39d3: 2006     JR nz,0x39db                 >> confirmed
  $39d5: cdb73c   CALL 0x3cb7                  .. static only
  $39d8: 23       INC HL                       .. static only
  $39d9: 18f5     JR 0x39d0                    .. static only
  $39db: cb7e     BIT 7,(HL)                   >> confirmed
  $39dd: 28cf     JR z,0x39ae                  >> confirmed
  $39df: 7e       LD A,(HL)                    >> confirmed
  $39e0: fe80     CP 0x80                      >> confirmed
  $39e2: 2807     JR z,0x39eb                  >> confirmed
  $39e4: 23       INC HL                       >> confirmed
  $39e5: 18f8     JR 0x39df                    >> confirmed
  $39e7: 23       INC HL                       .. static only
  $39e8: cdb73c   CALL 0x3cb7                  .. static only
  $39eb: 23       INC HL                       >> confirmed
  $39ec: 7e       LD A,(HL)                    >> confirmed
  $39ed: 23       INC HL                       >> confirmed
  $39ee: 66       LD H,(HL)                    >> confirmed
  $39ef: 6f       LD L,A                       >> confirmed
  $39f0: e9       JP (HL)                      >> confirmed
  $39f1: ff       RST 0x38                     .. static only
  $39f2: ff       RST 0x38                     .. static only
  $39f3: ff       RST 0x38                     .. static only
  $39f4: ff       RST 0x38                     .. static only
  $39f5: ff       RST 0x38                     .. static only
  $39f6: ff       RST 0x38                     .. static only
  $39f7: ff       RST 0x38                     .. static only
  $39f8: ff       RST 0x38                     .. static only
  $39f9: ff       RST 0x38                     .. static only
  $39fa: ff       RST 0x38                     .. static only
  $39fb: ff       RST 0x38                     .. static only
  $39fc: ff       RST 0x38                     .. static only
  $39fd: ff       RST 0x38                     .. static only
  $39fe: ff       RST 0x38                     .. static only
  $39ff: ff       RST 0x38                     .. static only
  $3a00: ff       RST 0x38                     .. static only
  $3a01: ff       RST 0x38                     .. static only
  $3a02: ff       RST 0x38                     .. static only
  $3a03: ff       RST 0x38                     .. static only
  $3a04: ff       RST 0x38                     .. static only
  $3a05: ff       RST 0x38                     .. static only
  $3a06: ff       RST 0x38                     .. static only
  $3a07: ff       RST 0x38                     .. static only
  $3a08: ff       RST 0x38                     .. static only
  $3a09: ff       RST 0x38                     .. static only
  $3a0a: ff       RST 0x38                     .. static only
  $3a0b: ff       RST 0x38                     .. static only
  $3a0c: ff       RST 0x38                     .. static only
  $3a0d: ff       RST 0x38                     .. static only
  $3a0e: ff       RST 0x38                     .. static only
  $3a0f: ff       RST 0x38                     .. static only
  $3a10: ff       RST 0x38                     .. static only
  $3a11: ff       RST 0x38                     .. static only
  $3a12: ff       RST 0x38                     .. static only
  $3a13: ff       RST 0x38                     .. static only
  $3a14: ff       RST 0x38                     .. static only
  $3a15: ff       RST 0x38                     .. static only
  $3a16: ff       RST 0x38                     .. static only
  $3a17: ff       RST 0x38                     .. static only
  $3a18: ff       RST 0x38                     .. static only
  $3a19: ff       RST 0x38                     .. static only
  $3a1a: ff       RST 0x38                     .. static only
  $3a1b: ff       RST 0x38                     .. static only
  $3a1c: ff       RST 0x38                     .. static only
  $3a1d: ff       RST 0x38                     .. static only
  $3a1e: ff       RST 0x38                     .. static only
  $3a1f: ff       RST 0x38                     .. static only
  $3a20: ff       RST 0x38                     .. static only
  $3a21: ff       RST 0x38                     .. static only
  $3a22: ff       RST 0x38                     .. static only
  $3a23: ff       RST 0x38                     .. static only
  $3a24: ff       RST 0x38                     .. static only
  $3a25: ff       RST 0x38                     .. static only
  $3a26: ff       RST 0x38                     .. static only
  $3a27: ff       RST 0x38                     .. static only
  $3a28: ff       RST 0x38                     .. static only
  $3a29: ff       RST 0x38                     .. static only
  $3a2a: ff       RST 0x38                     .. static only
  $3a2b: ff       RST 0x38                     .. static only
  $3a2c: ff       RST 0x38                     .. static only
  $3a2d: ff       RST 0x38                     .. static only
  $3a2e: ff       RST 0x38                     .. static only
  $3a2f: ff       RST 0x38                     .. static only
  $3a30: ff       RST 0x38                     .. static only
  $3a31: ff       RST 0x38                     .. static only
  $3a32: ff       RST 0x38                     .. static only
  $3a33: ff       RST 0x38                     .. static only
  $3a34: ff       RST 0x38                     .. static only
  $3a35: ff       RST 0x38                     .. static only
  $3a36: ff       RST 0x38                     .. static only
  $3a37: ff       RST 0x38                     .. static only
  $3a38: ff       RST 0x38                     .. static only
  $3a39: ff       RST 0x38                     .. static only
  $3a3a: ff       RST 0x38                     .. static only
  $3a3b: ff       RST 0x38                     .. static only
  $3a3c: ff       RST 0x38                     .. static only
  $3a3d: ff       RST 0x38                     .. static only
  $3a3e: ff       RST 0x38                     .. static only
  $3a3f: ff       RST 0x38                     .. static only
  $3a40: ff       RST 0x38                     .. static only
  $3a41: ff       RST 0x38                     .. static only
  $3a42: ff       RST 0x38                     .. static only
  $3a43: ff       RST 0x38                     .. static only
  $3a44: ff       RST 0x38                     .. static only
  $3a45: ff       RST 0x38                     .. static only
  $3a46: ff       RST 0x38                     .. static only
  $3a47: ff       RST 0x38                     .. static only
  $3a48: ff       RST 0x38                     .. static only
  $3a49: ff       RST 0x38                     .. static only
  $3a4a: ff       RST 0x38                     .. static only
  $3a4b: ff       RST 0x38                     .. static only
  $3a4c: ff       RST 0x38                     .. static only
  $3a4d: ff       RST 0x38                     .. static only
  $3a4e: ff       RST 0x38                     .. static only
  $3a4f: ff       RST 0x38                     .. static only
  $3a50: ff       RST 0x38                     .. static only
  $3a51: ff       RST 0x38                     .. static only
  $3a52: ff       RST 0x38                     .. static only
  $3a53: ff       RST 0x38                     .. static only
  $3a54: ff       RST 0x38                     .. static only
  $3a55: ff       RST 0x38                     .. static only
  $3a56: ff       RST 0x38                     .. static only
  $3a57: ff       RST 0x38                     .. static only
  $3a58: ff       RST 0x38                     .. static only
  $3a59: ff       RST 0x38                     .. static only
  $3a5a: ff       RST 0x38                     .. static only
  $3a5b: ff       RST 0x38                     .. static only
  $3a5c: ff       RST 0x38                     .. static only
  $3a5d: ff       RST 0x38                     .. static only
  $3a5e: ff       RST 0x38                     .. static only
  $3a5f: ff       RST 0x38                     .. static only
  $3a60: ff       RST 0x38                     .. static only
  $3a61: ff       RST 0x38                     .. static only
  $3a62: ff       RST 0x38                     .. static only
  $3a63: ff       RST 0x38                     .. static only
  $3a64: ff       RST 0x38                     .. static only
  $3a65: ff       RST 0x38                     .. static only
  $3a66: ff       RST 0x38                     .. static only
  $3a67: ff       RST 0x38                     .. static only
  $3a68: ff       RST 0x38                     .. static only
  $3a69: ff       RST 0x38                     .. static only
  $3a6a: ff       RST 0x38                     .. static only
  $3a6b: ff       RST 0x38                     .. static only
  $3a6c: ff       RST 0x38                     .. static only
  $3a6d: ff       RST 0x38                     .. static only
  $3a6e: ff       RST 0x38                     .. static only
  $3a6f: ff       RST 0x38                     .. static only
  $3a70: ff       RST 0x38                     .. static only
  $3a71: ff       RST 0x38                     .. static only
  $3a72: ff       RST 0x38                     .. static only
  $3a73: ff       RST 0x38                     .. static only
  $3a74: ff       RST 0x38                     .. static only
  $3a75: ff       RST 0x38                     .. static only
  $3a76: ff       RST 0x38                     .. static only
  $3a77: ff       RST 0x38                     .. static only
  $3a78: ff       RST 0x38                     .. static only
  $3a79: ff       RST 0x38                     .. static only
  $3a7a: ff       RST 0x38                     .. static only
  $3a7b: ff       RST 0x38                     .. static only
  $3a7c: ff       RST 0x38                     .. static only
  $3a7d: ff       RST 0x38                     .. static only
  $3a7e: ff       RST 0x38                     .. static only
  $3a7f: ff       RST 0x38                     .. static only
  $3a80: ff       RST 0x38                     .. static only
  $3a81: ff       RST 0x38                     .. static only
  $3a82: ff       RST 0x38                     .. static only
  $3a83: ff       RST 0x38                     .. static only
  $3a84: ff       RST 0x38                     .. static only
  $3a85: ff       RST 0x38                     .. static only
  $3a86: ff       RST 0x38                     .. static only
  $3a87: ff       RST 0x38                     .. static only
  $3a88: ff       RST 0x38                     .. static only
  $3a89: ff       RST 0x38                     .. static only
  $3a8a: ff       RST 0x38                     .. static only
  $3a8b: ff       RST 0x38                     .. static only
  $3a8c: ff       RST 0x38                     .. static only
  $3a8d: ff       RST 0x38                     .. static only
  $3a8e: ff       RST 0x38                     .. static only
  $3a8f: ff       RST 0x38                     .. static only
  $3a90: ff       RST 0x38                     .. static only
  $3a91: ff       RST 0x38                     .. static only
  $3a92: ff       RST 0x38                     .. static only
  $3a93: ff       RST 0x38                     .. static only
  $3a94: ff       RST 0x38                     .. static only
  $3a95: ff       RST 0x38                     .. static only
  $3a96: ff       RST 0x38                     .. static only
  $3a97: ff       RST 0x38                     .. static only
  $3a98: ff       RST 0x38                     .. static only
  $3a99: ff       RST 0x38                     .. static only
  $3a9a: ff       RST 0x38                     .. static only
  $3a9b: ff       RST 0x38                     .. static only
  $3a9c: ff       RST 0x38                     .. static only
  $3a9d: ff       RST 0x38                     .. static only
  $3a9e: ff       RST 0x38                     .. static only
  $3a9f: ff       RST 0x38                     .. static only
  $3aa0: ff       RST 0x38                     .. static only
  $3aa1: ff       RST 0x38                     .. static only
  $3aa2: ff       RST 0x38                     .. static only
  $3aa3: ff       RST 0x38                     .. static only
  $3aa4: ff       RST 0x38                     .. static only
  $3aa5: ff       RST 0x38                     .. static only
  $3aa6: ff       RST 0x38                     .. static only
  $3aa7: ff       RST 0x38                     .. static only
  $3aa8: ff       RST 0x38                     .. static only
  $3aa9: ff       RST 0x38                     .. static only
  $3aaa: ff       RST 0x38                     .. static only
  $3aab: ff       RST 0x38                     .. static only
  $3aac: ff       RST 0x38                     .. static only
  $3aad: ff       RST 0x38                     .. static only
  $3aae: ff       RST 0x38                     .. static only
  $3aaf: ff       RST 0x38                     .. static only
  $3ab0: ff       RST 0x38                     .. static only
  $3ab1: ff       RST 0x38                     .. static only
  $3ab2: ff       RST 0x38                     .. static only
  $3ab3: ff       RST 0x38                     .. static only
  $3ab4: ff       RST 0x38                     .. static only
  $3ab5: ff       RST 0x38                     .. static only
  $3ab6: ff       RST 0x38                     .. static only
  $3ab7: ff       RST 0x38                     .. static only
  $3ab8: ff       RST 0x38                     .. static only
  $3ab9: ff       RST 0x38                     .. static only
  $3aba: ff       RST 0x38                     .. static only
  $3abb: ff       RST 0x38                     .. static only
  $3abc: ff       RST 0x38                     .. static only
  $3abd: ff       RST 0x38                     .. static only
  $3abe: ff       RST 0x38                     .. static only
  $3abf: ff       RST 0x38                     .. static only
  $3ac0: ff       RST 0x38                     .. static only
  $3ac1: ff       RST 0x38                     .. static only
  $3ac2: ff       RST 0x38                     .. static only
  $3ac3: ff       RST 0x38                     .. static only
  $3ac4: ff       RST 0x38                     .. static only
  $3ac5: ff       RST 0x38                     .. static only
  $3ac6: ff       RST 0x38                     .. static only
  $3ac7: ff       RST 0x38                     .. static only
  $3ac8: ff       RST 0x38                     .. static only
  $3ac9: ff       RST 0x38                     .. static only
  $3aca: ff       RST 0x38                     .. static only
  $3acb: ff       RST 0x38                     .. static only
  $3acc: ff       RST 0x38                     .. static only
  $3acd: ff       RST 0x38                     .. static only
  $3ace: ff       RST 0x38                     .. static only
  $3acf: ff       RST 0x38                     .. static only
  $3ad0: ff       RST 0x38                     .. static only
  $3ad1: ff       RST 0x38                     .. static only
  $3ad2: ff       RST 0x38                     .. static only
  $3ad3: ff       RST 0x38                     .. static only
  $3ad4: ff       RST 0x38                     .. static only
  $3ad5: ff       RST 0x38                     .. static only
  $3ad6: ff       RST 0x38                     .. static only
  $3ad7: ff       RST 0x38                     .. static only
  $3ad8: ff       RST 0x38                     .. static only
  $3ad9: ff       RST 0x38                     .. static only
  $3ada: ff       RST 0x38                     .. static only
  $3adb: ff       RST 0x38                     .. static only
  $3adc: ff       RST 0x38                     .. static only
  $3add: ff       RST 0x38                     .. static only
  $3ade: ff       RST 0x38                     .. static only
  $3adf: ff       RST 0x38                     .. static only
  $3ae0: ff       RST 0x38                     .. static only
  $3ae1: ff       RST 0x38                     .. static only
  $3ae2: ff       RST 0x38                     .. static only
  $3ae3: ff       RST 0x38                     .. static only
  $3ae4: ff       RST 0x38                     .. static only
  $3ae5: ff       RST 0x38                     .. static only
  $3ae6: ff       RST 0x38                     .. static only
  $3ae7: ff       RST 0x38                     .. static only
  $3ae8: ff       RST 0x38                     .. static only
  $3ae9: ff       RST 0x38                     .. static only
  $3aea: ff       RST 0x38                     .. static only
  $3aeb: ff       RST 0x38                     .. static only
  $3aec: ff       RST 0x38                     .. static only
  $3aed: ff       RST 0x38                     .. static only
  $3aee: ff       RST 0x38                     .. static only
  $3aef: ff       RST 0x38                     .. static only
  $3af0: ff       RST 0x38                     .. static only
  $3af1: ff       RST 0x38                     .. static only
  $3af2: ff       RST 0x38                     .. static only
  $3af3: ff       RST 0x38                     .. static only
  $3af4: ff       RST 0x38                     .. static only
  $3af5: ff       RST 0x38                     .. static only
  $3af6: ff       RST 0x38                     .. static only
  $3af7: ff       RST 0x38                     .. static only
  $3af8: ff       RST 0x38                     .. static only
  $3af9: ff       RST 0x38                     .. static only
  $3afa: ff       RST 0x38                     .. static only
  $3afb: ff       RST 0x38                     .. static only
  $3afc: ff       RST 0x38                     .. static only
  $3afd: ff       RST 0x38                     .. static only
  $3afe: ff       RST 0x38                     .. static only
  $3aff: ff       RST 0x38                     .. static only
  $3b00: ff       RST 0x38                     .. static only
  $3b01: ff       RST 0x38                     .. static only
  $3b02: ff       RST 0x38                     .. static only
  $3b03: ff       RST 0x38                     .. static only
  $3b04: ff       RST 0x38                     .. static only
  $3b05: ff       RST 0x38                     .. static only
  $3b06: ff       RST 0x38                     .. static only
  $3b07: ff       RST 0x38                     .. static only
  $3b08: ff       RST 0x38                     .. static only
  $3b09: ff       RST 0x38                     .. static only
  $3b0a: ff       RST 0x38                     .. static only
  $3b0b: ff       RST 0x38                     .. static only
  $3b0c: ff       RST 0x38                     .. static only
  $3b0d: ff       RST 0x38                     .. static only
  $3b0e: ff       RST 0x38                     .. static only
  $3b0f: ff       RST 0x38                     .. static only
  $3b10: ff       RST 0x38                     .. static only
  $3b11: ff       RST 0x38                     .. static only
  $3b12: ff       RST 0x38                     .. static only
  $3b13: ff       RST 0x38                     .. static only
  $3b14: ff       RST 0x38                     .. static only
  $3b15: ff       RST 0x38                     .. static only
  $3b16: ff       RST 0x38                     .. static only
  $3b17: ff       RST 0x38                     .. static only
  $3b18: ff       RST 0x38                     .. static only
  $3b19: ff       RST 0x38                     .. static only
  $3b1a: ff       RST 0x38                     .. static only
  $3b1b: ff       RST 0x38                     .. static only
  $3b1c: ff       RST 0x38                     .. static only
  $3b1d: ff       RST 0x38                     .. static only
  $3b1e: ff       RST 0x38                     .. static only
  $3b1f: ff       RST 0x38                     .. static only
  $3b20: ff       RST 0x38                     .. static only
  $3b21: ff       RST 0x38                     .. static only
  $3b22: ff       RST 0x38                     .. static only
  $3b23: ff       RST 0x38                     .. static only
  $3b24: ff       RST 0x38                     .. static only
  $3b25: ff       RST 0x38                     .. static only
  $3b26: ff       RST 0x38                     .. static only
  $3b27: ff       RST 0x38                     .. static only
  $3b28: ff       RST 0x38                     .. static only
  $3b29: ff       RST 0x38                     .. static only
  $3b2a: ff       RST 0x38                     .. static only
  $3b2b: ff       RST 0x38                     .. static only
  $3b2c: ff       RST 0x38                     .. static only
  $3b2d: ff       RST 0x38                     .. static only
  $3b2e: ff       RST 0x38                     .. static only
  $3b2f: ff       RST 0x38                     .. static only
  $3b30: ff       RST 0x38                     .. static only
  $3b31: ff       RST 0x38                     .. static only
  $3b32: ff       RST 0x38                     .. static only
  $3b33: ff       RST 0x38                     .. static only
  $3b34: ff       RST 0x38                     .. static only
  $3b35: ff       RST 0x38                     .. static only
  $3b36: ff       RST 0x38                     .. static only
  $3b37: ff       RST 0x38                     .. static only
  $3b38: ff       RST 0x38                     .. static only
  $3b39: ff       RST 0x38                     .. static only
  $3b3a: ff       RST 0x38                     .. static only
  $3b3b: ff       RST 0x38                     .. static only
  $3b3c: ff       RST 0x38                     .. static only
  $3b3d: ff       RST 0x38                     .. static only
  $3b3e: ff       RST 0x38                     .. static only
  $3b3f: ff       RST 0x38                     .. static only
  $3b40: ff       RST 0x38                     .. static only
  $3b41: ff       RST 0x38                     .. static only
  $3b42: ff       RST 0x38                     .. static only
  $3b43: ff       RST 0x38                     .. static only
  $3b44: ff       RST 0x38                     .. static only
  $3b45: ff       RST 0x38                     .. static only
  $3b46: ff       RST 0x38                     .. static only
  $3b47: ff       RST 0x38                     .. static only
  $3b48: ff       RST 0x38                     .. static only
  $3b49: ff       RST 0x38                     .. static only
  $3b4a: ff       RST 0x38                     .. static only
  $3b4b: ff       RST 0x38                     .. static only
  $3b4c: ff       RST 0x38                     .. static only
  $3b4d: ff       RST 0x38                     .. static only
  $3b4e: ff       RST 0x38                     .. static only
  $3b4f: ff       RST 0x38                     .. static only
  $3b50: ff       RST 0x38                     .. static only
  $3b51: ff       RST 0x38                     .. static only
  $3b52: ff       RST 0x38                     .. static only
  $3b53: ff       RST 0x38                     .. static only
  $3b54: ff       RST 0x38                     .. static only
  $3b55: ff       RST 0x38                     .. static only
  $3b56: ff       RST 0x38                     .. static only
  $3b57: ff       RST 0x38                     .. static only
  $3b58: ff       RST 0x38                     .. static only
  $3b59: ff       RST 0x38                     .. static only
  $3b5a: ff       RST 0x38                     .. static only
  $3b5b: ff       RST 0x38                     .. static only
  $3b5c: ff       RST 0x38                     .. static only
  $3b5d: ff       RST 0x38                     .. static only
  $3b5e: ff       RST 0x38                     .. static only
  $3b5f: ff       RST 0x38                     .. static only
  $3b60: ff       RST 0x38                     .. static only
  $3b61: ff       RST 0x38                     .. static only
  $3b62: ff       RST 0x38                     .. static only
  $3b63: ff       RST 0x38                     .. static only
  $3b64: ff       RST 0x38                     .. static only
  $3b65: ff       RST 0x38                     .. static only
  $3b66: ff       RST 0x38                     .. static only
  $3b67: ff       RST 0x38                     .. static only
  $3b68: ff       RST 0x38                     .. static only
  $3b69: ff       RST 0x38                     .. static only
  $3b6a: ff       RST 0x38                     .. static only
  $3b6b: ff       RST 0x38                     .. static only
  $3b6c: ff       RST 0x38                     .. static only
  $3b6d: ff       RST 0x38                     .. static only
  $3b6e: ff       RST 0x38                     .. static only
  $3b6f: ff       RST 0x38                     .. static only
  $3b70: ff       RST 0x38                     .. static only
  $3b71: ff       RST 0x38                     .. static only
  $3b72: ff       RST 0x38                     .. static only
  $3b73: ff       RST 0x38                     .. static only
  $3b74: ff       RST 0x38                     .. static only
  $3b75: ff       RST 0x38                     .. static only
  $3b76: ff       RST 0x38                     .. static only
  $3b77: ff       RST 0x38                     .. static only
  $3b78: ff       RST 0x38                     .. static only
  $3b79: ff       RST 0x38                     .. static only
  $3b7a: ff       RST 0x38                     .. static only
  $3b7b: ff       RST 0x38                     .. static only
  $3b7c: ff       RST 0x38                     .. static only
  $3b7d: ff       RST 0x38                     .. static only
  $3b7e: ff       RST 0x38                     .. static only
  $3b7f: ff       RST 0x38                     .. static only
  $3b80: ff       RST 0x38                     .. static only
  $3b81: ff       RST 0x38                     .. static only
  $3b82: ff       RST 0x38                     .. static only
  $3b83: ff       RST 0x38                     .. static only
  $3b84: ff       RST 0x38                     .. static only
  $3b85: ff       RST 0x38                     .. static only
  $3b86: ff       RST 0x38                     .. static only
  $3b87: ff       RST 0x38                     .. static only
  $3b88: ff       RST 0x38                     .. static only
  $3b89: ff       RST 0x38                     .. static only
  $3b8a: ff       RST 0x38                     .. static only
  $3b8b: ff       RST 0x38                     .. static only
  $3b8c: ff       RST 0x38                     .. static only
  $3b8d: ff       RST 0x38                     .. static only
  $3b8e: ff       RST 0x38                     .. static only
  $3b8f: ff       RST 0x38                     .. static only
  $3b90: ff       RST 0x38                     .. static only
  $3b91: ff       RST 0x38                     .. static only
  $3b92: ff       RST 0x38                     .. static only
  $3b93: ff       RST 0x38                     .. static only
  $3b94: ff       RST 0x38                     .. static only
  $3b95: ff       RST 0x38                     .. static only
  $3b96: ff       RST 0x38                     .. static only
  $3b97: ff       RST 0x38                     .. static only
  $3b98: ff       RST 0x38                     .. static only
  $3b99: ff       RST 0x38                     .. static only
  $3b9a: ff       RST 0x38                     .. static only
  $3b9b: ff       RST 0x38                     .. static only
  $3b9c: ff       RST 0x38                     .. static only
  $3b9d: ff       RST 0x38                     .. static only
  $3b9e: ff       RST 0x38                     .. static only
  $3b9f: ff       RST 0x38                     .. static only
  $3ba0: ff       RST 0x38                     .. static only
  $3ba1: ff       RST 0x38                     .. static only
  $3ba2: ff       RST 0x38                     .. static only
  $3ba3: ff       RST 0x38                     .. static only
  $3ba4: ff       RST 0x38                     .. static only
  $3ba5: ff       RST 0x38                     .. static only
  $3ba6: ff       RST 0x38                     .. static only
  $3ba7: ff       RST 0x38                     .. static only
  $3ba8: ff       RST 0x38                     .. static only
  $3ba9: ff       RST 0x38                     .. static only
  $3baa: ff       RST 0x38                     .. static only
  $3bab: ff       RST 0x38                     .. static only
  $3bac: ff       RST 0x38                     .. static only
  $3bad: ff       RST 0x38                     .. static only
  $3bae: ff       RST 0x38                     .. static only
  $3baf: ff       RST 0x38                     .. static only
  $3bb0: ff       RST 0x38                     .. static only
  $3bb1: ff       RST 0x38                     .. static only
  $3bb2: ff       RST 0x38                     .. static only
  $3bb3: ff       RST 0x38                     .. static only
  $3bb4: ff       RST 0x38                     .. static only
  $3bb5: ff       RST 0x38                     .. static only
  $3bb6: ff       RST 0x38                     .. static only
  $3bb7: ff       RST 0x38                     .. static only
  $3bb8: ff       RST 0x38                     .. static only
  $3bb9: ff       RST 0x38                     .. static only
  $3bba: ff       RST 0x38                     .. static only
  $3bbb: ff       RST 0x38                     .. static only
  $3bbc: ff       RST 0x38                     .. static only
  $3bbd: ff       RST 0x38                     .. static only
  $3bbe: ff       RST 0x38                     .. static only
  $3bbf: ff       RST 0x38                     .. static only
  $3bc0: ff       RST 0x38                     .. static only
  $3bc1: ff       RST 0x38                     .. static only
  $3bc2: ff       RST 0x38                     .. static only
  $3bc3: ff       RST 0x38                     .. static only
  $3bc4: ff       RST 0x38                     .. static only
  $3bc5: ff       RST 0x38                     .. static only
  $3bc6: ff       RST 0x38                     .. static only
  $3bc7: ff       RST 0x38                     .. static only
  $3bc8: ff       RST 0x38                     .. static only
  $3bc9: ff       RST 0x38                     .. static only
  $3bca: ff       RST 0x38                     .. static only
  $3bcb: ff       RST 0x38                     .. static only
  $3bcc: ff       RST 0x38                     .. static only
  $3bcd: ff       RST 0x38                     .. static only
  $3bce: ff       RST 0x38                     .. static only
  $3bcf: ff       RST 0x38                     .. static only
  $3bd0: ff       RST 0x38                     .. static only
  $3bd1: ff       RST 0x38                     .. static only
  $3bd2: ff       RST 0x38                     .. static only
  $3bd3: ff       RST 0x38                     .. static only
  $3bd4: ff       RST 0x38                     .. static only
  $3bd5: ff       RST 0x38                     .. static only
  $3bd6: ff       RST 0x38                     .. static only
  $3bd7: ff       RST 0x38                     .. static only
  $3bd8: ff       RST 0x38                     .. static only
  $3bd9: ff       RST 0x38                     .. static only
  $3bda: ff       RST 0x38                     .. static only
  $3bdb: ff       RST 0x38                     .. static only
  $3bdc: ff       RST 0x38                     .. static only
  $3bdd: ff       RST 0x38                     .. static only
  $3bde: ff       RST 0x38                     .. static only
  $3bdf: ff       RST 0x38                     .. static only
  $3be0: ff       RST 0x38                     .. static only
  $3be1: ff       RST 0x38                     .. static only
  $3be2: ff       RST 0x38                     .. static only
  $3be3: ff       RST 0x38                     .. static only
  $3be4: ff       RST 0x38                     .. static only
  $3be5: ff       RST 0x38                     .. static only
  $3be6: ff       RST 0x38                     .. static only
  $3be7: ff       RST 0x38                     .. static only
  $3be8: ff       RST 0x38                     .. static only
  $3be9: ff       RST 0x38                     .. static only
  $3bea: ff       RST 0x38                     .. static only
  $3beb: ff       RST 0x38                     .. static only
  $3bec: ff       RST 0x38                     .. static only
  $3bed: ff       RST 0x38                     .. static only
  $3bee: ff       RST 0x38                     .. static only
  $3bef: ff       RST 0x38                     .. static only
  $3bf0: ff       RST 0x38                     .. static only
  $3bf1: ff       RST 0x38                     .. static only
  $3bf2: ff       RST 0x38                     .. static only
  $3bf3: ff       RST 0x38                     .. static only
  $3bf4: ff       RST 0x38                     .. static only
  $3bf5: ff       RST 0x38                     .. static only
  $3bf6: ff       RST 0x38                     .. static only
  $3bf7: ff       RST 0x38                     .. static only
  $3bf8: ff       RST 0x38                     .. static only
  $3bf9: ff       RST 0x38                     .. static only
  $3bfa: ff       RST 0x38                     .. static only
  $3bfb: ff       RST 0x38                     .. static only
  $3bfc: ff       RST 0x38                     .. static only
  $3bfd: ff       RST 0x38                     .. static only
  $3bfe: ff       RST 0x38                     .. static only
  $3bff: ff       RST 0x38                     .. static only
  $3c00: f3       DI                           .. static only
  $3c01: f3       DI                           .. static only
  $3c02: f3       DI                           .. static only
  $3c03: f3       DI                           .. static only
  $3c04: f3       DI                           .. static only
  $3c05: af       XOR A                        .. static only
  $3c06: 01fd1f   LD BC,0x1ffd                 .. static only
  $3c09: ed79     OUT (C),A                    .. static only
  $3c0b: 067f     LD B,0x7f                    .. static only
  $3c0d: 3e10     LD A,0x10                    .. static only
  $3c0f: ed79     OUT (C),A                    .. static only
  $3c11: c7       RST 0                        .. static only
  $3c12: ff       RST 0x38                     .. static only
  $3c13: ff       RST 0x38                     .. static only
  $3c14: ff       RST 0x38                     .. static only
  $3c15: ff       RST 0x38                     .. static only
  $3c16: ff       RST 0x38                     .. static only
  $3c17: ff       RST 0x38                     .. static only
  $3c18: f3       DI                           >> confirmed
  $3c19: 2a3d5c   LD HL,(0x5c3d)               >> confirmed
  $3c1c: 115f3c   LD DE,0x3c5f                 >> confirmed
  $3c1f: 73       LD (HL),E                    >> confirmed
  $3c20: 23       INC HL                       >> confirmed
  $3c21: 72       LD (HL),D                    >> confirmed
  $3c22: 21b05c   LD HL,0x5cb0                 >> confirmed
  $3c25: cb7e     BIT 7,(HL)                   >> confirmed
  $3c27: 2029     JR nz,0x3c52                 >> confirmed
  $3c29: 3680     LD (HL),0x80                 >> confirmed
  $3c2b: 07       RLCA                         >> confirmed
  $3c2c: cd9b22   CALL 0x229b                  >> confirmed
  $3c2f: ff       RST 0x38                     >> confirmed
  $3c30: 8d       ADC A,L                      .. static only
  $3c31: 5c       LD E,H                       .. static only
  $3c32: cd6b0d   CALL 0x0d6b                  >> confirmed
  $3c35: ff       RST 0x38                     >> confirmed
  $3c36: fecd     CP 0xcd                      .. static only
  $3c38: 011621   LD BC,0x2116                 .. static only
  $3c3b: 13       INC DE                       .. static only
  $3c3c: 33       INC SP                       .. static only
  $3c3d: ed5b7b5c LD DE,(0x5c7b)               >> confirmed
  $3c41: 011800   LD BC,0x18                   >> confirmed
  $3c44: edb0     LDIR                         >> confirmed
  $3c46: 0664     LD B,0x64                    >> confirmed
  $3c48: 21ad32   LD HL,0x32ad                 >> confirmed
  $3c4b: 7e       LD A,(HL)                    >> confirmed
  $3c4c: cd2b3e   CALL 0x3e2b                  >> confirmed
  $3c4f: 23       INC HL                       >> confirmed
  $3c50: 10f9     DJNZ 0x3c4b                  >> confirmed
  $3c52: 3e49     LD A,0x49                    >> confirmed
  $3c54: 32925c   LD (0x5c92),A                >> confirmed
  $3c57: cd333f   CALL PARSE_QUOTED_FILENAME   >> confirmed
  $3c5a: cd473f   CALL 0x3f47                  >> confirmed
  $3c5d: fb       EI                           >> confirmed
  $3c5e: c9       RET                          >> confirmed
  $3c5f: ff       RST 0x38                     >> confirmed
  $3c60: 83       ADD A,E                      .. static only
  $3c61: 3e3a     LD A,0x3a                    .. static only
  $3c63: 3a5cfe   LD A,(0xfe5c)                .. static only
  $3c66: 0b       DEC BC                       .. static only
  $3c67: 2828     JR z,0x3c91                  >> confirmed
  $3c69: cd3025   CALL 0x2530                  >> confirmed
  $3c6c: ff       RST 0x38                     >> confirmed
  $3c6d: 200e     JR nz,0x3c7d                 >> confirmed
  $3c6f: 215f3c   LD HL,0x3c5f                 .. static only
  $3c72: e5       PUSH HL                      .. static only
  $3c73: c3b712   JP 0x12b7                    .. static only
  $3c76: e1       POP HL                       .. static only
  $3c77: fd36000b LD (IY),11                   .. static only
  $3c7b: 18ec     JR 0x3c69                    .. static only
  $3c7d: cd0313   CALL 0x1303                  >> confirmed
  $3c80: ff       RST 0x38                     >> confirmed
  $3c81: 3600     LD (HL),0                    .. static only
  $3c83: ff       RST 0x38                     .. static only
  $3c84: 2a595c   LD HL,(0x5c59)               >> confirmed
  $3c87: cda711   CALL 0x11a7                  >> confirmed
  $3c8a: ff       RST 0x38                     >> confirmed
  $3c8b: 5f       LD E,A                       .. static only
  $3c8c: 3c       INC A                        .. static only
  $3c8d: e5       PUSH HL                      >> confirmed
  $3c8e: c3b412   JP 0x12b4                    >> confirmed
  $3c91: 2a5d5c   LD HL,(0x5c5d)               >> confirmed
  $3c94: 2b       DEC HL                       >> confirmed
  $3c95: 7e       LD A,(HL)                    >> confirmed
  $3c96: fe2a     CP 0x2a                      >> confirmed
  $3c98: 20cf     JR nz,0x3c69                 >> confirmed
  $3c9a: f3       DI                           >> confirmed
  $3c9b: 215f3c   LD HL,0x3c5f                 >> confirmed
  $3c9e: e5       PUSH HL                      >> confirmed
  $3c9f: fd3600ff LD (IY),0xff                 >> confirmed
  $3ca3: fd362600 LD (IY+0x26),0               >> confirmed
  $3ca7: 2a5d5c   LD HL,(0x5c5d)               >> confirmed
  $3caa: 22985c   LD (0x5c98),HL               >> confirmed
  $3cad: c3a539   JP 0x39a5                    >> confirmed
  $3cb0: e5       PUSH HL                      >> confirmed
  $3cb1: df       RST 0x18                     >> confirmed
  $3cb2: ff       RST 0x38                     >> confirmed
  $3cb3: be       CP (HL)                      .. static only
  $3cb4: 3c       INC A                        .. static only
  $3cb5: e1       POP HL                       >> confirmed
  $3cb6: c9       RET                          >> confirmed
  $3cb7: e5       PUSH HL                      >> confirmed
  $3cb8: e7       RST 0x20                     >> confirmed
  $3cb9: ff       RST 0x38                     >> confirmed
  $3cba: be       CP (HL)                      .. static only
  $3cbb: 3c       INC A                        .. static only
  $3cbc: e1       POP HL                       >> confirmed
  $3cbd: c9       RET                          >> confirmed
  $3cbe: fe7b     CP 0x7b                      >> confirmed
  $3cc0: d0       RET nc                       >> confirmed
  $3cc1: fe61     CP 0x61                      >> confirmed
  $3cc3: d8       RET c                        >> confirmed
  $3cc4: e6df     AND 0xdf                     .. static only
  $3cc6: c9       RET                          .. static only
  $3cc7: af       XOR A                        .. static only
  $3cc8: 180a     JR 0x3cd4                    .. static only
  $3cca: 3e01     LD A,1                       >> confirmed
  $3ccc: 1806     JR 0x3cd4                    >> confirmed
  $3cce: 3e02     LD A,2                       .. static only
  $3cd0: 1802     JR 0x3cd4                    .. static only
  $3cd2: 3e03     LD A,3                       .. static only
  $3cd4: fd3600ff LD (IY),0xff                 >> confirmed
  $3cd8: cd3025   CALL 0x2530                  >> confirmed
  $3cdb: ff       RST 0x38                     >> confirmed
  $3cdc: 13       INC DE                       .. static only
  $3cdd: cd2336   CALL 0x3623                  >> confirmed
  $3ce0: 21b712   LD HL,0x12b7                 >> confirmed
  $3ce3: e5       PUSH HL                      >> confirmed
  $3ce4: fb       EI                           >> confirmed
  $3ce5: c3761b   JP 0x1b76                    >> confirmed
  $3ce8: fdcb017e BIT 7,(IY+1)                 >> confirmed
  $3cec: 200c     JR nz,0x3cfa                 >> confirmed
  $3cee: 18f0     JR 0x3ce0                    >> confirmed
  $3cf0: cd2436   CALL 0x3624                  >> confirmed
  $3cf3: af       XOR A                        .. static only
  $3cf4: 32a05c   LD (0x5ca0),A                .. static only
  $3cf7: cd8133   CALL 0x3381                  .. static only
  $3cfa: fb       EI                           >> confirmed
  $3cfb: c3761b   JP 0x1b76                    >> confirmed
  $3cfe: cd3025   CALL 0x2530                  >> confirmed
  $3d01: 00       NOP                          >> confirmed
  $3d02: 05       DEC B                        .. static only
  $3d03: cd313e   CALL 0x3e31                  >> confirmed
  $3d06: 00       NOP                          >> confirmed
  $3d07: e0       RET po                       .. static only
  $3d08: cd323e   CALL 0x3e32                  >> confirmed
  $3d0b: 18db     JR 0x3ce8                    >> confirmed
  $3d0d: cd3025   CALL 0x2530                  .. static only
  $3d10: 2005     JR nz,0x3d17                 .. static only
  $3d12: cd473e   CALL 0x3e47                  .. static only
  $3d15: 18d1     JR 0x3ce8                    .. static only
  $3d17: cd483e   CALL 0x3e48                  .. static only
  $3d1a: 18cc     JR 0x3ce8                    .. static only
  $3d1c: 3e4e     LD A,0x4e                    .. static only
  $3d1e: 32925c   LD (0x5c92),A                .. static only
  $3d21: 18ea     JR 0x3d0d                    .. static only
  $3d23: 3e43     LD A,0x43                    .. static only
  $3d25: 18f7     JR 0x3d1e                    .. static only
  $3d27: 3e52     LD A,0x52                    .. static only
  $3d29: 18f3     JR 0x3d1e                    .. static only
  $3d2b: 3e53     LD A,0x53                    .. static only
  $3d2d: 18ef     JR 0x3d1e                    .. static only
  $3d2f: 3e56     LD A,0x56                    .. static only
  $3d31: 1802     JR 0x3d35                    .. static only
  $3d33: 3e49     LD A,0x49                    .. static only
  $3d35: 32925c   LD (0x5c92),A                .. static only
  $3d38: cd3025   CALL 0x2530                  .. static only
  $3d3b: 2005     JR nz,0x3d42                 .. static only
  $3d3d: cd2f3f   CALL 0x3f2f                  .. static only
  $3d40: 18a6     JR 0x3ce8                    .. static only
  $3d42: cd303f   CALL 0x3f30                  .. static only
  $3d45: 18a1     JR 0x3ce8                    .. static only
  $3d47: cdee1b   CALL 0x1bee                  .. static only
  $3d4a: c38033   JP 0x3380                    .. static only
  $3d4d: cd3025   CALL 0x2530                  .. static only
  $3d50: 2005     JR nz,0x3d57                 .. static only
  $3d52: cda93e   CALL 0x3ea9                  .. static only
  $3d55: 1891     JR 0x3ce8                    .. static only
  $3d57: cdaa3e   CALL 0x3eaa                  .. static only
  $3d5a: 188c     JR 0x3ce8                    .. static only
  $3d5c: df       RST 0x18                     .. static only
  $3d5d: fe2c     CP 0x2c                      .. static only
  $3d5f: c28a1c   JP nz,0x1c8a                 .. static only
  $3d62: e7       RST 0x20                     .. static only
  $3d63: cd821c   CALL 0x1c82                  .. static only
  $3d66: cd3025   CALL 0x2530                  .. static only
  $3d69: 2005     JR nz,0x3d70                 .. static only
  $3d6b: e5       PUSH HL                      .. static only
  $3d6c: cdee1b   CALL 0x1bee                  .. static only
  $3d6f: e1       POP HL                       .. static only
  $3d70: cd941e   CALL 0x1e94                  .. static only
  $3d73: 32945c   LD (0x5c94),A                .. static only
  $3d76: c3c733   JP 0x33c7                    .. static only
  $3d79: cd3025   CALL 0x2530                  >> confirmed
  $3d7c: 08       EX AF,AF'                    >> confirmed
  $3d7d: 2d       DEC L                        .. static only
  $3d7e: 36cd     LD (HL),0xcd                 .. static only
  $3d80: 0116c3   LD BC,0xc316                 .. static only
  $3d83: 34       INC (HL)                     .. static only
  $3d84: 36cd     LD (HL),0xcd                 .. static only
  $3d86: 8c       ADC A,H                      .. static only
  $3d87: 1c       INC E                        .. static only
  $3d88: 00       NOP                          >> confirmed
  $3d89: 3025     JR nc,0x3db0                 .. static only
  $3d8b: 08       EX AF,AF'                    >> confirmed
  $3d8c: 37       SCF                          .. static only
  $3d8d: 36f7     LD (HL),0xf7                 .. static only
  $3d8f: 00       NOP                          >> confirmed
  $3d90: 47       LD B,A                       .. static only
  $3d91: 36cd     LD (HL),0xcd                 .. static only
  $3d93: f1       POP AF                       .. static only
  $3d94: 2b       DEC HL                       .. static only
  $3d95: 40       LD B,B                       >> confirmed
  $3d96: 55       LD D,L                       .. static only
  $3d97: 36cd     LD (HL),0xcd                 .. static only
  $3d99: 3025     JR nc,0x3dc0                 .. static only
  $3d9b: 0c       INC C                        >> confirmed
  $3d9c: 84       ADD A,H                      .. static only
  $3d9d: 36df     LD (HL),0xdf                 .. static only
  $3d9f: 00       NOP                          >> confirmed
  $3da0: 9c       SBC A,H                      .. static only
  $3da1: 36e7     LD (HL),0xe7                 .. static only
  $3da3: cdb228   CALL 0x28b2                  .. static only
  $3da6: c3ab36   JP 0x36ab                    .. static only
  $3da9: cd3025   CALL 0x2530                  .. static only
  $3dac: c3c136   JP 0x36c1                    .. static only
  $3daf: e7       RST 0x20                     .. static only
  $3db0: c3df36   JP 0x36df                    .. static only
  $3db3: e7       RST 0x20                     .. static only
  $3db4: cdee1b   CALL 0x1bee                  .. static only
  $3db7: c3e636   JP 0x36e6                    .. static only
  $3dba: e7       RST 0x20                     .. static only
  $3dbb: cdee1b   CALL 0x1bee                  .. static only
  $3dbe: c3f936   JP 0x36f9                    .. static only
  $3dc1: e7       RST 0x20                     .. static only
  $3dc2: cd4820   CALL 0x2048                  .. static only
  $3dc5: c31b37   JP 0x371b                    .. static only
  $3dc8: cde61c   CALL 0x1ce6                  .. static only
  $3dcb: 1807     JR 0x3dd4                    .. static only
  $3dcd: cd821c   CALL 0x1c82                  .. static only
  $3dd0: df       RST 0x18                     .. static only
  $3dd1: c32a37   JP 0x372a                    .. static only
  $3dd4: cde61c   CALL 0x1ce6                  .. static only
  $3dd7: c3de3d   JP 0x3dde                    .. static only
  $3dda: e7       RST 0x20                     .. static only
  $3ddb: cd821c   CALL 0x1c82                  .. static only
  $3dde: cdee1b   CALL 0x1bee                  .. static only
  $3de1: cd991e   CALL 0x1e99                  .. static only
  $3de4: dd7101   LD (IX+1),C                  .. static only
  $3de7: dd7002   LD (IX+2),B                  .. static only
  $3dea: cd991e   CALL 0x1e99                  .. static only
  $3ded: c33937   JP 0x3739                    .. static only
  $3df0: cdee1b   CALL 0x1bee                  >> confirmed
  $3df3: 08       EX AF,AF'                    >> confirmed
  $3df4: 4e       LD C,(HL)                    .. static only
  $3df5: 37       SCF                          .. static only
  $3df6: e7       RST 0x20                     .. static only
  $3df7: cd821c   CALL 0x1c82                  .. static only
  $3dfa: cdee1b   CALL 0x1bee                  .. static only
  $3dfd: cd991e   CALL 0x1e99                  .. static only
  $3e00: c35e37   JP 0x375e                    .. static only
TRAMP_CALL1601_TO_379D:
  $3e03: cd0116   CALL HOST_ROM_1601           >> confirmed
  $3e06: 3c       INC A                        >> confirmed
  $3e07: 9d       SBC A,L                      .. static only
  $3e08: 37       SCF                          .. static only
  $3e09: cd051f   CALL 0x1f05                  .. static only
  $3e0c: c32038   JP 0x3820                    .. static only
  $3e0f: cde819   CALL 0x19e8                  .. static only
  $3e12: c33a38   JP 0x383a                    .. static only
  $3e15: cd5516   CALL 0x1655                  .. static only
  $3e18: c35338   JP 0x3853                    .. static only
  $3e1b: cde519   CALL 0x19e5                  .. static only
  $3e1e: c37938   JP 0x3879                    .. static only
  $3e21: cd5516   CALL 0x1655                  .. static only
  $3e24: c37f38   JP 0x387f                    .. static only
  $3e27: f7       RST 0x30                     .. static only
  $3e28: c3b738   JP 0x38b7                    .. static only
  $3e2b: f5       PUSH AF                      >> confirmed
  $3e2c: fb       EI                           >> confirmed
  $3e2d: d7       RST 0x10                     >> confirmed
  $3e2e: 7e       LD A,(HL)                    >> confirmed
  $3e2f: f1       POP AF                       >> confirmed
  $3e30: c9       RET                          >> confirmed
  $3e31: e5       PUSH HL                      >> confirmed
  $3e32: cdee1b   CALL 0x1bee                  >> confirmed
  $3e35: 40       LD B,B                       >> confirmed
  $3e36: de38     SBC A,0x38                   .. static only
  $3e38: f7       RST 0x30                     >> confirmed
  $3e39: 3c       INC A                        >> confirmed
  $3e3a: ed38                                  .. static only
  $3e3c: cd2b2d   CALL 0x2d2b                  >> confirmed
  $3e3f: 00       NOP                          >> confirmed
  $3e40: cde32d   CALL 0x2de3                  >> confirmed
  $3e43: 7e       LD A,(HL)                    >> confirmed
  $3e44: c35439   JP 0x3954                    >> confirmed
  $3e47: e5       PUSH HL                      .. static only
  $3e48: cd3025   CALL 0x2530                  .. static only
  $3e4b: 2805     JR z,0x3e52                  .. static only
  $3e4d: 3efe     LD A,0xfe                    .. static only
  $3e4f: cd0116   CALL HOST_ROM_1601           .. static only
  $3e52: cd8c1c   CALL 0x1c8c                  .. static only
  $3e55: cdee1b   CALL 0x1bee                  .. static only
  $3e58: cdf12b   CALL 0x2bf1                  .. static only
  $3e5b: c37939   JP 0x3979                    .. static only
  $3e5e: f7       RST 0x30                     .. static only
  $3e5f: c38539   JP 0x3985                    .. static only
  $3e62: cd7333   CALL 0x3373                  .. static only
  $3e65: c3e83c   JP 0x3ce8                    .. static only
  $3e68: cd5c3d   CALL 0x3d5c                  .. static only
  $3e6b: c3e83c   JP 0x3ce8                    .. static only
  $3e6e: c39c5c   JP 0x5c9c                    .. static only
  $3e71: 3e98     LD A,0x98                    >> confirmed
  $3e73: d3ff     OUT (0x00ff),A               >> confirmed
  $3e75: c9       RET                          >> confirmed
CALL1601_A_FE_RET:
  $3e76: 3efe     LD A,0xfe                    >> confirmed
  $3e78: cd0116   CALL HOST_ROM_1601           >> confirmed
  $3e7b: 42       LD B,D                       >> confirmed
  $3e7c: fb       EI                           >> confirmed
  $3e7d: c37b1b   JP 0x1b7b                    >> confirmed
  $3e80: 00       NOP                          >> confirmed
  $3e81: 183c     JR 0x3ebf                    .. static only
  $3e83: 21925c   LD HL,0x5c92                 >> confirmed
  $3e86: 010010   LD BC,0x1000                 >> confirmed
  $3e89: af       XOR A                        >> confirmed
  $3e8a: 77       LD (HL),A                    >> confirmed
  $3e8b: 23       INC HL                       >> confirmed
  $3e8c: 10fc     DJNZ 0x3e8a                  >> confirmed
  $3e8e: c9       RET                          >> confirmed
  $3e8f: 21925c   LD HL,0x5c92                 .. static only
  $3e92: cbb6     RES 6,(HL)                   .. static only
  $3e94: 3ab05c   LD A,(0x5cb0)                .. static only
  $3e97: e603     AND 3                        .. static only
  $3e99: c608     ADD A,8                      .. static only
  $3e9b: cd7530   CALL IEC_SEND_DEVNUM         .. static only
  $3e9e: 3e6f     LD A,0x6f                    .. static only
  $3ea0: cd5d33   CALL 0x335d                  .. static only
  $3ea3: 21925c   LD HL,0x5c92                 .. static only
  $3ea6: cbb6     RES 6,(HL)                   .. static only
  $3ea8: c9       RET                          .. static only
  $3ea9: e5       PUSH HL                      .. static only
  $3eaa: cd3025   CALL 0x2530                  .. static only
  $3ead: 2805     JR z,0x3eb4                  .. static only
  $3eaf: 3efe     LD A,0xfe                    .. static only
  $3eb1: cd0116   CALL HOST_ROM_1601           .. static only
  $3eb4: cd8c1c   CALL 0x1c8c                  .. static only
  $3eb7: cdee1b   CALL 0x1bee                  .. static only
  $3eba: cdf12b   CALL 0x2bf1                  .. static only
  $3ebd: 79       LD A,C                       >> confirmed
  $3ebe: b7       OR A                         >> confirmed
  $3ebf: c8       RET z                        >> confirmed
  $3ec0: 03       INC BC                       >> confirmed
  $3ec1: 78       LD A,B                       >> confirmed
  $3ec2: 0b       DEC BC                       >> confirmed
  $3ec3: b7       OR A                         >> confirmed
  $3ec4: c24206   JP nz,0x0642                 >> confirmed
  $3ec7: c5       PUSH BC                      >> confirmed
  $3ec8: 0c       INC C                        >> confirmed
  $3ec9: 79       LD A,C                       >> confirmed
  $3eca: 329c5c   LD (0x5c9c),A                >> confirmed
  $3ecd: d5       PUSH DE                      >> confirmed
  $3ece: f7       RST 0x30                     >> confirmed
  $3ecf: 00       NOP                          >> confirmed
  $3ed0: 53       LD D,E                       .. static only
  $3ed1: 9e       SBC A,(HL)                   .. static only
  $3ed2: 5c       LD E,H                       .. static only
  $3ed3: e1       POP HL                       >> confirmed
  $3ed4: c1       POP BC                       >> confirmed
  $3ed5: edb0     LDIR                         >> confirmed
  $3ed7: c39639   JP LOAD_OPEN_SHARED_CONT     >> confirmed
  $3eda: df       RST 0x18                     .. static only
  $3edb: fe2c     CP 0x2c                      .. static only
  $3edd: c28a1c   JP nz,0x1c8a                 .. static only
  $3ee0: e7       RST 0x20                     .. static only
  $3ee1: cd821c   CALL 0x1c82                  .. static only
  $3ee4: cd3025   CALL 0x2530                  .. static only
  $3ee7: 2005     JR nz,0x3eee                 .. static only
  $3ee9: e5       PUSH HL                      .. static only
  $3eea: cdee1b   CALL 0x1bee                  .. static only
  $3eed: e1       POP HL                       .. static only
  $3eee: cd941e   CALL 0x1e94                  .. static only
  $3ef1: fe08     CP 8                         .. static only
  $3ef3: da8a1c   JP c,0x1c8a                  .. static only
  $3ef6: fe0c     CP 12                        .. static only
  $3ef8: d28a1c   JP nc,0x1c8a                 .. static only
  $3efb: 47       LD B,A                       .. static only
  $3efc: 21b05c   LD HL,0x5cb0                 .. static only
  $3eff: 7e       LD A,(HL)                    .. static only
  $3f00: e6fc     AND 0xfc                     .. static only
  $3f02: b0       OR B                         .. static only
  $3f03: 77       LD (HL),A                    .. static only
  $3f04: c3e83c   JP 0x3ce8                    .. static only
  $3f07: e3       EX (SP),HL                   .. static only
  $3f08: e5       PUSH HL                      .. static only
  $3f09: dde5     PUSH IX                      .. static only
  $3f0b: d5       PUSH DE                      .. static only
  $3f0c: f5       PUSH AF                      .. static only
  $3f0d: dd210200 LD IX,2                      .. static only
  $3f11: dd39     ADD IX,SP                    .. static only
  $3f13: f1       POP AF                       .. static only
  $3f14: 5e       LD E,(HL)                    .. static only
  $3f15: 23       INC HL                       .. static only
  $3f16: 56       LD D,(HL)                    .. static only
  $3f17: 23       INC HL                       .. static only
  $3f18: dd7304   LD (IX+4),E                  .. static only
  $3f1b: dd7205   LD (IX+5),D                  .. static only
  $3f1e: dd5e06   LD E,(IX+6)                  .. static only
  $3f21: dd7506   LD (IX+6),L                  .. static only
  $3f24: dd5607   LD D,(IX+7)                  .. static only
  $3f27: dd7407   LD (IX+7),H                  .. static only
  $3f2a: eb       EX DE,HL                     .. static only
  $3f2b: d1       POP DE                       .. static only
  $3f2c: dde1     POP IX                       .. static only
  $3f2e: c9       RET                          .. static only
  $3f2f: e5       PUSH HL                      .. static only
  $3f30: cdee1b   CALL 0x1bee                  .. static only
PARSE_QUOTED_FILENAME:
  $3f33: 010200   LD BC,2                      >> confirmed
  $3f36: 79       LD A,C                       >> confirmed
  $3f37: 329c5c   LD (0x5c9c),A                >> confirmed
  $3f3a: f7       RST 0x30                     >> confirmed
  $3f3b: 44       LD B,H                       >> confirmed
  $3f3c: 92       SUB D                        .. static only
  $3f3d: 5c       LD E,H                       .. static only
  $3f3e: 12       LD (DE),A                    >> confirmed
  $3f3f: ed539e5c LD (0x5c9e),DE               >> confirmed
  $3f43: 13       INC DE                       >> confirmed
  $3f44: c39639   JP LOAD_OPEN_SHARED_CONT     >> confirmed
  $3f47: 11503f   LD DE,0x3f50                 >> confirmed
  $3f4a: 010400   LD BC,4                      >> confirmed
  $3f4d: c3bd3e   JP 0x3ebd                    >> confirmed
  $3f50: 55       LD D,L                       .. static only
  $3f51: 49       LD C,C                       .. static only
  $3f52: 2d       DEC L                        .. static only
  $3f53: 0d       DEC C                        .. static only
  $3f54: ff       RST 0x38                     .. static only
  $3f55: ff       RST 0x38                     .. static only
  $3f56: ff       RST 0x38                     .. static only
  $3f57: ff       RST 0x38                     .. static only
  $3f58: ff       RST 0x38                     .. static only
  $3f59: ff       RST 0x38                     .. static only
  $3f5a: ff       RST 0x38                     .. static only
  $3f5b: ff       RST 0x38                     .. static only
  $3f5c: ff       RST 0x38                     .. static only
  $3f5d: ff       RST 0x38                     .. static only
  $3f5e: ff       RST 0x38                     .. static only
  $3f5f: ff       RST 0x38                     .. static only
  $3f60: ff       RST 0x38                     .. static only
  $3f61: ff       RST 0x38                     .. static only
  $3f62: ff       RST 0x38                     .. static only
  $3f63: ff       RST 0x38                     .. static only
  $3f64: ff       RST 0x38                     .. static only
  $3f65: ff       RST 0x38                     .. static only
  $3f66: ff       RST 0x38                     .. static only
  $3f67: ff       RST 0x38                     .. static only
  $3f68: ff       RST 0x38                     .. static only
  $3f69: ff       RST 0x38                     .. static only
  $3f6a: ff       RST 0x38                     .. static only
  $3f6b: ff       RST 0x38                     .. static only
  $3f6c: ff       RST 0x38                     .. static only
  $3f6d: ff       RST 0x38                     .. static only
  $3f6e: ff       RST 0x38                     .. static only
  $3f6f: ff       RST 0x38                     .. static only
  $3f70: ff       RST 0x38                     .. static only
  $3f71: ff       RST 0x38                     .. static only
  $3f72: ff       RST 0x38                     .. static only
  $3f73: ff       RST 0x38                     .. static only
  $3f74: ff       RST 0x38                     .. static only
  $3f75: ff       RST 0x38                     .. static only
  $3f76: ff       RST 0x38                     .. static only
  $3f77: ff       RST 0x38                     .. static only
  $3f78: ff       RST 0x38                     .. static only
  $3f79: ff       RST 0x38                     .. static only
  $3f7a: ff       RST 0x38                     .. static only
  $3f7b: ff       RST 0x38                     .. static only
  $3f7c: ff       RST 0x38                     .. static only
  $3f7d: ff       RST 0x38                     .. static only
  $3f7e: ff       RST 0x38                     .. static only
  $3f7f: ff       RST 0x38                     .. static only
  $3f80: ff       RST 0x38                     .. static only
  $3f81: ff       RST 0x38                     .. static only
  $3f82: ff       RST 0x38                     .. static only
  $3f83: ff       RST 0x38                     .. static only
  $3f84: ff       RST 0x38                     .. static only
  $3f85: ff       RST 0x38                     .. static only
  $3f86: ff       RST 0x38                     .. static only
  $3f87: ff       RST 0x38                     .. static only
  $3f88: ff       RST 0x38                     .. static only
  $3f89: ff       RST 0x38                     .. static only
  $3f8a: ff       RST 0x38                     .. static only
  $3f8b: ff       RST 0x38                     .. static only
  $3f8c: ff       RST 0x38                     .. static only
  $3f8d: ff       RST 0x38                     .. static only
  $3f8e: ff       RST 0x38                     .. static only
  $3f8f: ff       RST 0x38                     .. static only
  $3f90: ff       RST 0x38                     .. static only
  $3f91: ff       RST 0x38                     .. static only
  $3f92: ff       RST 0x38                     .. static only
  $3f93: ff       RST 0x38                     .. static only
  $3f94: ff       RST 0x38                     .. static only
  $3f95: ff       RST 0x38                     .. static only
  $3f96: ff       RST 0x38                     .. static only
  $3f97: ff       RST 0x38                     .. static only
  $3f98: ff       RST 0x38                     .. static only
  $3f99: ff       RST 0x38                     .. static only
  $3f9a: ff       RST 0x38                     .. static only
  $3f9b: ff       RST 0x38                     .. static only
  $3f9c: ff       RST 0x38                     .. static only
  $3f9d: ff       RST 0x38                     .. static only
  $3f9e: ff       RST 0x38                     .. static only
  $3f9f: ff       RST 0x38                     .. static only
  $3fa0: ff       RST 0x38                     .. static only
  $3fa1: ff       RST 0x38                     .. static only
  $3fa2: ff       RST 0x38                     .. static only
  $3fa3: ff       RST 0x38                     .. static only
  $3fa4: ff       RST 0x38                     .. static only
  $3fa5: ff       RST 0x38                     .. static only
  $3fa6: ff       RST 0x38                     .. static only
  $3fa7: ff       RST 0x38                     .. static only
  $3fa8: ff       RST 0x38                     .. static only
  $3fa9: ff       RST 0x38                     .. static only
  $3faa: ff       RST 0x38                     .. static only
  $3fab: ff       RST 0x38                     .. static only
  $3fac: ff       RST 0x38                     .. static only
  $3fad: ff       RST 0x38                     .. static only
  $3fae: ff       RST 0x38                     .. static only
  $3faf: ff       RST 0x38                     .. static only
  $3fb0: ff       RST 0x38                     .. static only
  $3fb1: ff       RST 0x38                     .. static only
  $3fb2: ff       RST 0x38                     .. static only
  $3fb3: ff       RST 0x38                     .. static only
  $3fb4: ff       RST 0x38                     .. static only
  $3fb5: ff       RST 0x38                     .. static only
  $3fb6: ff       RST 0x38                     .. static only
  $3fb7: ff       RST 0x38                     .. static only
  $3fb8: ff       RST 0x38                     .. static only
  $3fb9: ff       RST 0x38                     .. static only
  $3fba: ff       RST 0x38                     .. static only
  $3fbb: ff       RST 0x38                     .. static only
  $3fbc: ff       RST 0x38                     .. static only
  $3fbd: ff       RST 0x38                     .. static only
  $3fbe: ff       RST 0x38                     .. static only
  $3fbf: ff       RST 0x38                     .. static only
  $3fc0: ff       RST 0x38                     .. static only
  $3fc1: ff       RST 0x38                     .. static only
  $3fc2: ff       RST 0x38                     .. static only
  $3fc3: ff       RST 0x38                     .. static only
  $3fc4: ff       RST 0x38                     .. static only
  $3fc5: ff       RST 0x38                     .. static only
  $3fc6: ff       RST 0x38                     .. static only
  $3fc7: ff       RST 0x38                     .. static only
  $3fc8: ff       RST 0x38                     .. static only
  $3fc9: ff       RST 0x38                     .. static only
  $3fca: ff       RST 0x38                     .. static only
  $3fcb: ff       RST 0x38                     .. static only
  $3fcc: ff       RST 0x38                     .. static only
  $3fcd: ff       RST 0x38                     .. static only
  $3fce: ff       RST 0x38                     .. static only
  $3fcf: ff       RST 0x38                     .. static only
  $3fd0: ff       RST 0x38                     .. static only
  $3fd1: ff       RST 0x38                     .. static only
  $3fd2: ff       RST 0x38                     .. static only
  $3fd3: ff       RST 0x38                     .. static only
  $3fd4: ff       RST 0x38                     .. static only
  $3fd5: ff       RST 0x38                     .. static only
  $3fd6: ff       RST 0x38                     .. static only
  $3fd7: ff       RST 0x38                     .. static only
  $3fd8: ff       RST 0x38                     .. static only
  $3fd9: ff       RST 0x38                     .. static only
  $3fda: ff       RST 0x38                     .. static only
  $3fdb: ff       RST 0x38                     .. static only
  $3fdc: ff       RST 0x38                     .. static only
  $3fdd: ff       RST 0x38                     .. static only
  $3fde: ff       RST 0x38                     .. static only
  $3fdf: ff       RST 0x38                     .. static only
  $3fe0: ff       RST 0x38                     .. static only
  $3fe1: ff       RST 0x38                     .. static only
  $3fe2: ff       RST 0x38                     .. static only
  $3fe3: ff       RST 0x38                     .. static only
  $3fe4: ff       RST 0x38                     .. static only
  $3fe5: ff       RST 0x38                     .. static only
  $3fe6: ff       RST 0x38                     .. static only
  $3fe7: ff       RST 0x38                     .. static only
  $3fe8: ff       RST 0x38                     .. static only
  $3fe9: ff       RST 0x38                     .. static only
  $3fea: ff       RST 0x38                     .. static only
  $3feb: ff       RST 0x38                     .. static only
  $3fec: ff       RST 0x38                     .. static only
  $3fed: ff       RST 0x38                     .. static only
  $3fee: ff       RST 0x38                     .. static only
  $3fef: ff       RST 0x38                     .. static only
  $3ff0: ff       RST 0x38                     .. static only
  $3ff1: ff       RST 0x38                     .. static only
  $3ff2: ff       RST 0x38                     .. static only
  $3ff3: ff       RST 0x38                     .. static only
  $3ff4: ff       RST 0x38                     .. static only
  $3ff5: ff       RST 0x38                     .. static only
  $3ff6: ff       RST 0x38                     .. static only
  $3ff7: ff       RST 0x38                     .. static only
  $3ff8: ff       RST 0x38                     .. static only
  $3ff9: ff       RST 0x38                     .. static only
  $3ffa: ff       RST 0x38                     .. static only
  $3ffb: ff       RST 0x38                     .. static only
  $3ffc: ff       RST 0x38                     .. static only
  $3ffd: ff       RST 0x38                     .. static only
  $3ffe: ff       RST 0x38                     .. static only
  $3fff: ff       RST 0x38                     .. static only
