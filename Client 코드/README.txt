1. fbtest.c를 수정하여 작성함
2. string.txt는 반드시 ANSI 포맷이어야 함
3. 따라서 string.txt는 Windows PC의 메모장에서 ANSI 포맷으로 설정 후 수정해야 함
4. string.txt를 UTF8 포맷으로 작성하면 안됨
5. string.txt의 파일 포맷 확인 법
file -i string.txt
위 명령 수행 시 아래와 같이 "charset=iso-8859-1"로 나와야 함
string.txt: text/plain; charset=iso-8859-1

