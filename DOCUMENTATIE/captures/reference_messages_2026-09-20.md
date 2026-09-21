# Reference SMS messages that worked (DM1701 OpenGD77 fork, 2026-09-20)
Known-good examples to compare against when something stops working. All are DMR Standard format (UDP 5016, UTF-16LE text, sub-header `00 0D 00 0A`), group messages over TGIF via the owner's WPSD hotspot (DMR ID 2358607, M7MYJ). Payloads are the IP packet as logged by the radio (`SMS TX payload` / `SMS RX payload`), after which the DMR data blocks carry it with pad octets and CRC32 (see `sms_send_format_choice.md`).
Integrity verified with `check_sms_payloads.py` (IPv4 header checksum, UDP checksum, trailing CRC32, group flag `E1`, port 5016): all four messages pass.
Routing needed for group SMS between the DM1701 and Mark: RF TG **23585** on slot 1 -> TGIF TG 23585 (`TGRewrite4` in the TGIF section of `/etc/dmrgateway`), because the radio only accepts a group SMS whose *embedded* destination is a TG in its receive list, and no gateway rewrite alters the destination inside a data burst.

## A. Mark (M7JVY, 2358452) -> DM1701, group, TGIF TG 23585
- Text: `JMGDAMJW`
- Received live over TGIF via the hotspot (RF TG 4023585 after the gateway rewrite). Decoded and stored in the Inbox at 21:19:27 BST. Sender radio: AnyTone-class, DMR Standard. 3 network preambles (8,7,6 to follow).
- Payload (60 bytes): IP src `0c23fcb4` dst `e1005c21`, UDP 5016 -> 5016; checks (IP, UDP, CRC32, group, port 5016) = (True, True, True, True, True)

```
45000032000F0000011173B30C23FCB4E1005C2113981398001E416F000D000A4A004D004700440041004D004A0057000000000000000000390D4102
```

Log excerpt (`[hh:mm:ss]` is PC local time = BST; hotspot line stamps are UTC):

```
[21:19:23] [HOTSPOT] M: 2026-09-20 20:19:22.165 DMR Slot 1, received network Data Preamble CSBK (8 to follow) from M7JVY to TG 4023585
[21:19:23] [HOTSPOT] M: 2026-09-20 20:19:22.402 DMR Slot 1, received network Data Preamble CSBK (7 to follow) from M7JVY to TG 4023585
[21:19:23] [HOTSPOT] M: 2026-09-20 20:19:22.542 DMR Slot 1, received network Data Preamble CSBK (6 to follow) from M7JVY to TG 4023585
[21:19:24] [HOTSPOT] M: 2026-09-20 20:19:22.779 DMR Slot 1, received network data header from M7JVY to TG 4023585, 5 blocks
[21:19:24] [HOTSPOT] M: 2026-09-20 20:19:22.790 DMR Slot 1, ended network data transmission
[21:19:27] [RADIO] SMS RX header accepted: dpf=2 group=1 src=2358452 blocks=5 pad=6
[21:19:27] [RADIO] SMS RX from=2358452 totalLength=60 padOctets=6
[21:19:27] [RADIO] SMS RX payload len=60: 45000032000F0000011173B30C23FCB4E1005C2113981398001E416F000D000A4A004D004700440041004D004A0057000000000000000000390D4102
[21:19:27] [RADIO] SMS RX decode OK: "JMGDAMJW"
[21:19:28] [RADIO] SMS inbox store OK: writeIndex=0 inboxCount=1
```

## B. DM1701 -> Mark, group, RF TG 23585 (slot 1) -> TGIF TG 23585
- Text: `test`
- First send after the TGIF `TGRewrite4=1,23585,1,23585,1` rule was added (21:27:53 BST). The owner confirmed Mark received a message and replied; he did not say which of B or C.
- Payload (48 bytes): IP src `0c23fd4f` dst `e1005c21`, UDP 5016 -> 5016; checks (IP, UDP, CRC32, group, port 5016) = (True, True, True, True, True)

```
45000028000300000111732E0C23FD4FE1005C2113981398001451E9000D000A5400450053005400000000008C1777C0
```

Log excerpt (`[hh:mm:ss]` is PC local time = BST; hotspot line stamps are UTC):

```
[21:27:53] [RADIO] SMS pack to=2147507233 from=2358607 format=1 result=0 text="test"
[21:27:53] [RADIO] SMS TX to=2147507233 from=2358607 text="test"
[21:27:53] [RADIO] SMS TX payload len=48: 45000028000300000111732E0C23FD4FE1005C2113981398001451E9000D000A5400450053005400000000008C1777C0
[21:27:53] [HOTSPOT] M: 2026-09-20 20:27:52.396 Downlink Activate received from M7MYJ
[21:27:54] [RADIO] HRC6000StartQueuedSMS: preambleCount=8 blockCount=4 frameCount=13 (SMS_MAX_TX_FRAMES=51) txSequence=0
[21:27:54] [RADIO] SMS TX started (keyed up)
[21:27:55] [HOTSPOT] M: 2026-09-20 20:27:53.993 DMR Slot 1, received RF Data Preamble CSBK (12 to follow) from M7MYJ to TG 23585
[21:27:55] [HOTSPOT] M: 2026-09-20 20:27:54.242 DMR Slot 1, received RF Data Preamble CSBK (11 to follow) from M7MYJ to TG 23585
[21:27:55] [HOTSPOT] M: 2026-09-20 20:27:54.361 DMR Slot 1, received RF Data Preamble CSBK (10 to follow) from M7MYJ to TG 23585
[21:27:55] [HOTSPOT] M: 2026-09-20 20:27:54.482 DMR Slot 1, received RF Data Preamble CSBK (9 to follow) from M7MYJ to TG 23585
[21:27:56] [HOTSPOT] M: 2026-09-20 20:27:54.612 DMR Slot 1, received RF Data Preamble CSBK (8 to follow) from M7MYJ to TG 23585
[21:27:56] [HOTSPOT] M: 2026-09-20 20:27:54.736 DMR Slot 1, received RF Data Preamble CSBK (7 to follow) from M7MYJ to TG 23585
[21:27:56] [HOTSPOT] M: 2026-09-20 20:27:54.867 DMR Slot 1, received RF Data Preamble CSBK (6 to follow) from M7MYJ to TG 23585
[21:27:56] [HOTSPOT] M: 2026-09-20 20:27:54.997 DMR Slot 1, received RF Data Preamble CSBK (5 to follow) from M7MYJ to TG 23585
[21:27:56] [RADIO] TX_END_1: smsActive TX complete, frameIndex=13 frameCount=13
[21:27:56] [HOTSPOT] M: 2026-09-20 20:27:55.243 DMR Slot 1, received RF data header from M7MYJ to TG 23585, 4 blocks
[21:27:56] [HOTSPOT] M: 2026-09-20 20:27:55.255 DMR Slot 1, ended RF data transmission
[21:27:57] [RADIO] SMS RX header accepted: dpf=2 group=1 src=2358607 blocks=4 pad=4
[21:27:57] [RADIO] SMS RX from=2358607 totalLength=48 padOctets=4
[21:27:58] [RADIO] SMS RX payload len=48: 45000028000300000111732E0C23FD4FE1005C2113981398001451E9000D000A5400450053005400000000008C1777C0
[21:27:58] [RADIO] SMS RX decode OK: "TEST"
[21:27:58] [RADIO] SMS inbox store OK: writeIndex=3 inboxCount=4

--- DMRGateway rule trace for this send (hotspot /var/log/pi-star/DMRGateway-2026-09-20.log) ---
8070:D: 2026-09-20 20:27:53.997 Network Received
8071:D: 2026-09-20 20:27:53.997 0000:  44 4D 52 44 00 23 FD 4F 00 5C 21 00 23 FD 4F 23    *DMRD.#.O.\!.#.O#*
8072:D: 2026-09-20 20:27:53.997 0010:  B3 19 88 1D 45 FF 48 FB 91 18 15 E4 53 88 32 70    *....E.H.....S.2p*
8073:D: 2026-09-20 20:27:53.997 0020:  84 CD FF 57 D7 5D F5 DA C9 6A 46 A8 2E B9 3E 44    *...W.]...jF...>D*
8074:D: 2026-09-20 20:27:53.997 0030:  2B D4 47 99 9B 00 2F                               *+.G.../*
8075:D: 2026-09-20 20:27:53.997 Rule Trace, RF transmission: Slot=1 Src=2358607 Dst=TG23585
8101:D: 2026-09-20 20:27:53.997 Rule Trace,	RewriteTG from TGIF_Network Slot=1 Dst=TG23585: matched
8102:D: 2026-09-20 20:27:53.997 Rule Trace,	RewriteTG to TGIF_Network Slot=1 Dst=TG23585
8103:D: 2026-09-20 20:27:53.997 Network Transmitted
8104:D: 2026-09-20 20:27:53.997 0000:  44 4D 52 44 00 23 FD 4F 00 5C 21 0E 0E F3 3F 23    *DMRD.#.O.\!...?#*
8105:D: 2026-09-20 20:27:53.997 0010:  B3 19 88 1D 45 FF 48 FB 91 18 15 E4 53 88 32 70    *....E.H.....S.2p*
8106:D: 2026-09-20 20:27:53.997 0020:  84 CD FF 57 D7 5D F5 DA C9 6A 46 A8 2E B9 3E 44    *...W.]...jF...>D*
8107:D: 2026-09-20 20:27:53.997 0030:  2B D4 47 99 9B 00 2F                               *+.G.../*

```

## C. DM1701 -> Mark, group, RF TG 23585 (slot 1) -> TGIF TG 23585
- Text: `mdjdmaap`
- Second send (21:29:43 BST). Same route as B.
- Payload (60 bytes): IP src `0c23fd4f` dst `e1005c21`, UDP 5016 -> 5016; checks (IP, UDP, CRC32, group, port 5016) = (True, True, True, True, True)

```
4500003000040000011173250C23FD4FE1005C2113981398001C53D8000D000A4D0044004A0044004D004100410050000000000000000000103DC980
```

Log excerpt (`[hh:mm:ss]` is PC local time = BST; hotspot line stamps are UTC):

```
[21:29:43] [RADIO] SMS pack to=2147507233 from=2358607 format=1 result=0 text="mdjdmaap"
[21:29:43] [RADIO] SMS TX to=2147507233 from=2358607 text="mdjdmaap"
[21:29:44] [HOTSPOT] M: 2026-09-20 20:29:42.850 Downlink Activate received from M7MYJ
[21:29:44] [RADIO] SMS TX payload len=60: 4500003000040000011173250C23FD4FE1005C2113981398001C53D8000D000A4D0044004A0044004D004100410050000000000000000000103DC980
[21:29:44] [RADIO] HRC6000StartQueuedSMS: preambleCount=8 blockCount=5 frameCount=14 (SMS_MAX_TX_FRAMES=51) txSequence=0
[21:29:44] [RADIO] SMS TX started (keyed up)
[21:29:45] [HOTSPOT] M: 2026-09-20 20:29:43.985 DMR Slot 1, received RF Data Preamble CSBK (13 to follow) from M7MYJ to TG 23585
[21:29:45] [HOTSPOT] M: 2026-09-20 20:29:44.235 DMR Slot 1, received RF Data Preamble CSBK (12 to follow) from M7MYJ to TG 23585
[21:29:45] [HOTSPOT] M: 2026-09-20 20:29:44.362 DMR Slot 1, received RF Data Preamble CSBK (11 to follow) from M7MYJ to TG 23585
[21:29:45] [HOTSPOT] M: 2026-09-20 20:29:44.489 DMR Slot 1, received RF Data Preamble CSBK (10 to follow) from M7MYJ to TG 23585
[21:29:46] [HOTSPOT] M: 2026-09-20 20:29:44.607 DMR Slot 1, received RF Data Preamble CSBK (9 to follow) from M7MYJ to TG 23585
[21:29:46] [HOTSPOT] M: 2026-09-20 20:29:44.737 DMR Slot 1, received RF Data Preamble CSBK (8 to follow) from M7MYJ to TG 23585
[21:29:46] [HOTSPOT] M: 2026-09-20 20:29:44.866 DMR Slot 1, received RF Data Preamble CSBK (7 to follow) from M7MYJ to TG 23585
[21:29:46] [HOTSPOT] M: 2026-09-20 20:29:45.000 DMR Slot 1, received RF Data Preamble CSBK (6 to follow) from M7MYJ to TG 23585
[21:29:46] [RADIO] TX_END_1: smsActive TX complete, frameIndex=14 frameCount=14
[21:29:46] [HOTSPOT] M: 2026-09-20 20:29:45.239 DMR Slot 1, received RF data header from M7MYJ to TG 23585, 5 blocks
[21:29:46] [HOTSPOT] M: 2026-09-20 20:29:45.256 DMR Slot 1, ended RF data transmission
[21:29:47] [RADIO] SMS RX header accepted: dpf=2 group=1 src=2358607 blocks=5 pad=8
[21:29:47] [RADIO] SMS RX from=2358607 totalLength=60 padOctets=8
[21:29:48] [RADIO] SMS RX payload len=60: 4500003000040000011173250C23FD4FE1005C2113981398001C53D8000D000A4D0044004A0044004D004100410050000000000000000000103DC980
[21:29:48] [RADIO] SMS RX decode OK: "MDJDMAAP"
[21:29:48] [RADIO] SMS inbox store OK: writeIndex=4 inboxCount=5
```

## D. M0WDG (Dave, 2342774, AnyTone 890) -> DM1701, group, real AnyTone packets replayed via the rig
- Text: `VIA TG M0WDG`
- Replay of his real captured packets (header + 5 data blocks, no preambles) through the replay rig (see hotspot_replay_rig_LOCAL_ONLY.md). Proves the radio decodes a real AnyTone DMR Standard message. Note IP dst E1 89 B0 61 = group 9023585, i.e. Dave's radio addresses the TGIF talkgroup as 9023585.
- Payload (60 bytes): IP src `0c23bf76` dst `e189b061`, UDP 5016 -> 5016; checks (IP, UDP, CRC32, group, port 5016) = (True, True, True, True, True)

```
450000380007000001115C2A0C23BF76E189B06113981398002460D7000D000A56004900410020005400470020004D003000570044004700A750EEAC
```

Log excerpt (`[hh:mm:ss]` is PC local time = BST; hotspot line stamps are UTC):

```
[22:40:33] [HOTSPOT] M: 2026-09-20 21:40:32.227 DMR Slot 1, received network data header from M0WDG to TG 4999999, 5 blocks
[22:40:33] [HOTSPOT] M: 2026-09-20 21:40:32.280 DMR Slot 1, ended network data transmission
[22:40:34] [RADIO] DATA RX type=6 crcValid=1 priv=0 readOk=1 slotState=1 b0=82 b1=40
[22:40:34] [RADIO] DATA RX type=7 crcValid=1 priv=0 readOk=1 slotState=1 b0=45 b1=00
[22:40:34] [RADIO] DATA RX type=7 crcValid=1 priv=0 readOk=1 slotState=1 b0=0C b1=23
[22:40:34] [RADIO] DATA RX type=7 crcValid=1 priv=0 readOk=1 slotState=1 b0=00 b1=24
[22:40:34] [RADIO] DATA RX type=7 crcValid=1 priv=0 readOk=1 slotState=1 b0=41 b1=00
[22:40:34] [RADIO] DATA RX type=7 crcValid=1 priv=0 readOk=1 slotState=1 b0=30 b1=00
[22:40:34] [RADIO] SMS RX from=2342774 totalLength=60 padOctets=0
[22:40:34] [RADIO] SMS RX payload len=60: 450000380007000001115C2A0C23BF76E189B06113981398002460D7000D000A56004900410020005400470020004D003000570044004700A750EEAC
[22:40:34] [RADIO] SMS RX decode OK: "VIA TG M0WDG"
[22:40:35] [RADIO] SMS inbox store OK: writeIndex=1 inboxCount=2
```

## Not proven end to end

- Private (unit-to-unit) messages to another user, and any ACK: no radio has ever returned an ACK, and group messages are not acknowledged.
- M0WDG -> DM1701 *live* messages: his real messages have reached the hotspot but the radio has not displayed them, except one from earlier in the day that arrived before the debug firmware. See the replay rig notes for the analysis.
