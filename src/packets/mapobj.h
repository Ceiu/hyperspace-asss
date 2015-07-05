
/* from priit's post to bugtraq, bugid 1139


u8 Type; //constant 10
 u16 PlayerID; //player id who's objects to change or 0xffff to change objects for everybody in arena
 u8 SubType; //constant 54
 X*(
 u16 MapObject:1;
 u16 Id:15;
 s16 X,Y;
 u8 ImageNr;
 u8 Layer;
 u16 DisplayTime:12; // 1/10th seconds
 u16 TimerMode:4;
 )

*/

