# SNAKE
## A port of Acornsoft Snake (BBC Micro, 1982) to the Acorn System 5

Target : System 5, 6502, 40x25 teletext VDU (&0400, 6845 at &0800),
          IMADOS / Acorn DOS, SID at &0C60  
Build  : beebasm -i snake.asm            (produces SNAKE)  
       : MakeSSD.cmd creates S5snake.ssd  
Run    : *RUN SNAKE   (load and exec address &2800)  
