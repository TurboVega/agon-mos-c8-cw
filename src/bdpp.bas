90 REM PRINT "Before: ": VDU &8C,&9D,&AE: PRINT "."
95 REM VDU 23,0,&C3: REM switch buffer
100 REM VDU 23,&8C,&70,&80,&60,&10,&EA,&15,&15,&11: REM BDPP_PACKET_START_MARKER
110 REM VDU 23,&9D,&63,&94,&E2,&81,&66,&18,&20,&18: REM BDPP_PACKET_ESCAPE
120 REM VDU 23,&AE,&70,&88,&F0,&80,&6A,&15,&15,&11: REM BDPP_PACKET_END_MARKER
130 REM PRINT "After: ": VDU &8C,&9D,&AE: PRINT "."

200 DIM code% 300
201 DIM fcn% 1: PRINT "fcn% ";fcn%
202 DIM index% 1: PRINT "index% ";index%
203 DIM flags% 1: PRINT "flags% ";flags%
204 DIM size% 2: PRINT "size% ";size%
205 DIM count% 2: PRINT "count% ";count%
206 DIM data% 2: PRINT "data% ";data%
210 PROC_assemble_bdpp
220 ?fcn%=0: rc%=USR(bdppFcn1%): PRINT "is_allowed -> ";rc%
230 ?fcn%=1: rc%=USR(bdppFcn1%): PRINT "is_enabled -> ";rc%
240 ?index%=0: ?fcn%=2: rc%=USR(bdppFcn2%): REM PRINT "enable -> ";rc%
250 PRINT "<<BDPP mode: This is in a packet!>>"
251 PRINT "Printing a 'Q' as one byte:";
252 ?fcn%=&0D: ?data%=&51: CALL bdppFcn4%
253 PRINT ""
254 PRINT "This needs 3 ESC characters:";
255 PRINT CHR$(&8C);CHR$(&9D);CHR$(&AE);"!"
256 PRINT "<About to disable packet mode>"
257 ?fcn%=&F: CALL bdppFcn3%
260 ?fcn%=3: rc%=USR(bdppFcn1%): PRINT "disable -> ";rc%
270 PRINT "((MOS mode: This is NOT in a packet.))"
999 END

49985 REM IX: Data address
49986 REM IY: Size of buffer or Count of bytes
49987 REM  D: Data byte
49988 REM  C: Packet Flags
49989 REM  B: Packet Index
49990 REM  A: BDPP function code
50000 DEF PROC_assemble_bdpp
50001 REM -- bdppFcn1% --
50002 REM BDPP uses several styles of function calls:
50004 REM BOOL bdpp_is_allowed();
50006 REM BOOL bdpp_is_enabled();
50010 REM BOOL bdpp_disable();
50012 P%=code%: bdppFcn1%=P%
50014 [OPT 2
50015 LD HL,fcn%: LD A,(HL)
50016 RST.LIL &20
50017 LD L,A
50018 LD H,0
50019 XOR A
50020 EXX
50021 XOR A
50022 LD L,0
50023 LD H,0
50024 LD C,A
50025 RET
50026 ]
50039 REM -- bdppFcn2% --
50040 REM BOOL bdpp_enable(BYTE stream);
50042 REM BOOL bdpp_is_tx_app_packet_done(BYTE index);
50044 REM BOOL bdpp_is_rx_app_packet_done(BYTE index);
50046 REM BYTE bdpp_get_rx_app_packet_flags(BYTE index);
50048 REM BOOL bdpp_stop_using_app_packet(BYTE index);
50050 bdppFcn2%=P%
50051 [OPT 2
50052 LD HL,index%: LD B,(HL)
50053 LD HL,fcn%: LD A,(HL)
50054 RST.LIL &20
50055 LD L,A
50056 LD H,0
50057 XOR A
50058 EXX
50059 XOR A
50060 LD L,0
50061 LD H,0
50062 LD C,A
50063 RET
50064 ]
50079 REM -- bdppFcn3% --
50080 REM void bdpp_flush_drv_tx_packet();
50082 bdppFcn3%=P%
50084 [OPT 2
50085 LD HL,fcn%: LD A,(HL)
50086 RST.LIL &20
50088 RET
50090 ]
50099 REM -- bdppFcn4% --
50100 REM void bdpp_write_byte_to_drv_tx_packet(BYTE data);
50101 REM void bdpp_write_drv_tx_byte_with_usage(BYTE data);
50102 bdppFcn4%=P%
50104 [OPT 2
50109 LD HL,fcn%: LD A,(HL)
50110 LD HL,data%: LD D,(HL)
50112 RST.LIL &20
50114 RET
50116 ]
50119 REM -- bdppFcn5% --
50120 REM void bdpp_write_bytes_to_drv_tx_packet(const BYTE* data, WORD count);
50122 REM void bdpp_write_drv_tx_bytes_with_usage(const BYTE* data, WORD count);
50124 bdppFcn5%=P%
50126 [OPT 2
50128 LD HL,count%: DEFB &ED: DEFB &37
50130 LD HL,fcn%: LD A,(HL)
50132 LD HL,data%: DEFB &ED: DEFB &31
50138 RST.LIL &20
50140 RET
50142 ]
50159 REM -- bdppFcn6% --
50160 REM BOOL bdpp_prepare_rx_app_packet(BYTE index, WORD size, BYTE* data);
50161 bdppFcn6%=P%
50162 [OPT 2
50163 LD HL,size%: DEFB &ED: DEFB &37
50164 LD HL,index%: LD B,(HL)
50165 LD HL,fcn%: LD A,(HL)
50166 LD HL,data%: DEFB &ED: DEFB &31
50167 RST.LIL &20
50168 LD L,A
50169 LD H,0
50170 XOR A
50171 EXX
50172 XOR A
50173 LD L,0
50174 LD H,0
50175 LD C,A
50176 RET
50178 ]
50198 REM -- bdppFcn7% --
50199 REM BOOL bdpp_queue_tx_app_packet(BYTE indexes, BYTE flags, const BYTE* data, WORD size);
50200 REM BOOL bdpp_prepare_tx_app_packet(BYTE indexes, BYTE flags, const BYTE* data, WORD size);
50201 bdppFcn7%=P%
50202 [OPT 2
50203 LD HL,size%: DEFB &ED: DEFB &37
50204 LD HL,flags%: LD D,(HL)
50205 LD HL,index%: LD B,(HL)
50206 LD HL,fcn%: LD A,(HL)
50207 LD HL,data%: DEFB &ED: DEFB &31
50208 RST.LIL &20
50209 LD L,A
50210 LD H,0
50211 XOR A
50212 EXX
50213 XOR A
50214 LD L,0
50215 LD H,0
50216 LD C,A
50217 RET
50218 ]
50219 REM -- bdppFcn8% --
50230 REM WORD bdpp_get_rx_app_packet_size(BYTE index);
50232 [OPT 2
50234 LD HL,index%: LD B,(HL)
50236 LD HL,fcn%: LD A,(HL)
50238 RST.LIL &20
50240 RET
50242 ]

50244 PRINT "bdppFcn1% ";~bdppFcn1%
50246 PRINT "bdppFcn2% ";~bdppFcn2%
50248 PRINT "bdppFcn3% ";~bdppFcn3%
50250 PRINT "bdppFcn4% ";~bdppFcn4%
50252 PRINT "bdppFcn5% ";~bdppFcn5%
50254 PRINT "bdppFcn6% ";~bdppFcn6%
50256 PRINT "bdppFcn7% ";~bdppFcn7%
50258 PRINT "total code: ";P%-code%
50260 ENDPROC
