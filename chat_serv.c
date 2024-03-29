

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>

#define BUF_SIZE 100
#define MAX_MEMBER 100
#define MAX_NAME_SIZE 20
#define MAX_ROOM 100
#define EMPTY 0
#define FULL 1
#define TRUE 1
#define FALSE 0
#define PIPE 3
#define NO_PIPE 0
#define DIE -1
#define CURE 1
#define error_message(x)   \
    {                      \
        printf("%s\n", x); \
    }
#define SWAP(x, y, z) \
    {                 \
        z = x;        \
        x = y;        \
        y = z;        \
    }

#define FREE_MESSAGE '0'
#define SYSTEM_MESSAGE '1'
#define USER_MESSAGE '2'
#define WHISPER_MESSAGE '3'
#define MAFIA_MESSAGE '4'
#define MAFIA_END_MESSAGE '5'
//free는 메세지만 보내고 싶을때
//sysmtem은 system출력하고 싶을때
//user은 사용자이름 출력하고 싶을 때 사용한다.

int room_max;
int min_pipe = 100;

void error_handling(char *buf);
typedef enum
{
    police,
    mafia,
    docter,
    soldier,
    citizen
} jobs;
typedef enum
{
    dead,
    alive
} life;
typedef enum
{
    use,
    unuse
} capacity;
typedef enum
{
    noon,
    vote,
    night
} today;

typedef struct
{
    char valid; //접속해있는 사람인지 아닌지 결정하는 변수
    char first; //처음입장했는지 아닌지 묻는 함수
    char room;
    char type;
    int mafia_num;
    int citizen_num;
    char name[MAX_NAME_SIZE];
    char message[BUF_SIZE];
    //마피아게임을 위한 변수
    char play; //마피아게임중인지 확인
    jobs job;
    life live;
    capacity skill;    //능력을 썼는지 유무
    char vote_num;     //투표를 얼만큼 받았는지 설정
    char skill_target; //스킬을 누구에게 쓸건지 정하는 함수  //군인은 이걸로 능력을 썼는지 안썼는지 결정하겠다. EMPTY면 아직 안쓴거 FULL이면 쓴거
} member;
//사용자 변수

typedef struct
{
    int block_member[MAX_MEMBER];
} blocking;

typedef struct
{
    int startgame;       //게임이 시작하고 있는지 아닌지 확인하는 변수
    int room_number;     //이방은 실제로 어느방인가
    int mem_number;      //처음 참가하고 있는 인원
    int out_member;      //나간인원이 있는가 체크함
    int to_main_pipe[2]; //파이프 연결
    int to_child_pipe[2];
    int mafia_number;
    int citizen_number;
    today day;           //지금 현재 낮인가 밤인가
    int member_list[12]; //참가하고 있는 전원의 파일디스크립터를 넣는 변수
} room_info;

struct arg
{
    int room_number;
    int mem_number;
    int fd_max;
};

blocking blocking_list[MAX_MEMBER]; //차단하기위한 변수
member member_list[MAX_MEMBER];
room_info room_mafia[MAX_ROOM]; //마피아하는 방을 위한 변수

//디버깅용 코드

//사용자를 담는 코드
int member_num;
void new_member(int num);

//사용자 초기화와 관련된 함수
int checking_name(member buf, int fd_max);                         //중복이름을 검사하는 함수
int alreay_print_room(int *room_list, int room_num, int fill_num); //방리스트를 검사하는 함수
void first_enter(member buf, int i, int fd_max);                   //처음들어왔을 때 이름설정 도와주는 함수
void first_room(member buf, int i, int fd_max);                    //처음왔을 때 방설정을 도와주는 함수
void out_room(member buf, fd_set *reads, int i, int fd_max);       //나갈때 정리하는 함수

//채팅과 관련된 함수
int receive_message(member *buf, int from);
void message_task(member buf, int i, int *fd_max, fd_set *); //메세지를 다루는 함수
void send_message(member buf, char type, int dest);          //메세지를 보내는 함수

//마피아와 관련된 함수
int for_mafia_room(int i);                                     //빈방을 찾아준다.
int start_mafia(int i, int *fd_max, fd_set *reads);            //마피아 게임을 시작하는 함수(쓰레드에 넘겨줄것들을 만들고 쓰레드를 생성함)
void *mafia_game(void *args);                                  //마피아게임 thread다
int initial_game(int mem_number, int room_pos);                //직업을 설정해준다.
int mafia_number(int room_pos, int *mafia_num, int *live_num); //마피아가 몇명 살아있는지 반환하는 함수
void mafia_send_message(member buf, char type, int room_pos);  //마피아 방에 있는 사람들에게 메세지를 보내는 함수
void end_mafia_game(int i, int room_pos, fd_set *reads);       //마피아 게임을 종료하고 자원을 반환하는 함수
void mafia_chat(int i, int fd_max, member buf, fd_set *reads); //마피아게임을 하는 사람들끼리 쓰는 채팅
void change_day(int i, member buf, fd_set *reads);             //마피아방의 현재 시간정보를 바꾸고 그에따라서 결산하는 함수
void result_vote(int room_pos);                                //투표결과
void result_night(int room_pos);                               //밤의 결과

int main(int argc, char *argv[])
{
    int serv_sock, clnt_sock;              //소켓 설정을 위한 소켓 생성
    struct sockaddr_in serv_adr, clnt_adr; //서버쪽 주소와 클라이언트쪽 주소를 저장하기위한 구조체
    struct timeval timeout;                //서버가 무한정 블로킹상태에 빠지지 않기 위한 timeout(연결을 기다릴때 계속 가만히 있을 수 있다.)
    fd_set reads, cpy_reads;               //이건 현재 연결되어 있는 사용자를 비트로 표현하는 것이다. (1이면 연결 0 이면 비었음)
    socklen_t adr_sz;
    int fd_max, str_len, fd_num, i;
    int room_list[MAX_ROOM]; //이까지는 소켓을 위한 변수

    member buf;              //버퍼 이걸로 통신함
    char buf_temp[BUF_SIZE]; //메세지 옮기기위한 변수
    int room_check = 0;      //방리스트 보여줄때 쓰는 변수
    char message_type[10];   //메세지 타입(FREE,USER,SYSTEM)

    //디버깅용
    signal(SIGQUIT, SIG_IGN); //무시합니다.

    if (argc != 2)
    {
        printf("Usage : %s <PORT> \n", argv[0]); //포트임의로 넣기위해서
        exit(1);
    }

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1)
        error_handling("socket() error");
    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port = htons(atoi(argv[1]));

    if (bind(serv_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)) == -1)
        error_handling("bind() error"); // bind는 이제 소켓에 주소를 할당하는 과정이다.

    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error"); //listen은 이제 소켓이 들을 수 있음을 말한다. 즉 연결요청이 오면 들을 수 있다.

    FD_ZERO(&reads); //아무것도 연결안되어 있으니까 전부 0으로 초기화하는 매크로함수

    FD_SET(serv_sock, &reads); //서버 소켓을 read목록에 등록해주는 함수다.

    fd_max = serv_sock;

    srand((long)time(NULL));
    memset(&buf, EMPTY, sizeof(buf));
    //서버가 지금부터 연결을 받을 수 있음
    while (1)
    {
        cpy_reads = reads; //select를 할경우 reads값이 다 바뀌는데 그럼 원본정보가 바뀌므로 그걸방지하기 위해 복사하는 과정
        timeout.tv_sec = 5;
        timeout.tv_usec = 5000; //5초마다 서버를 블로킹에서 풀어주려는 과정 select에서 서버가 멈추고 있기 때문이다.

        if ((fd_num = select(fd_max + 1, &cpy_reads, 0, 0, &timeout)) == -1)
        {
            break;
        }
        if (fd_num == 0)
        {
            //블로킹이 풀렸을 때 어떤 코드를 넣고 싶다면 넣으면 됨
            continue;
        }
        for (i = 0; i < fd_max + 1; i++)
        {
            if (FD_ISSET(i, &cpy_reads))
            {
                if (i == serv_sock) //서버연결 요청
                {
                    member_num++;
                    //연결요청이 왔으므로 멤버수 증가
                    adr_sz = sizeof(clnt_adr);
                    if (member_num < MAX_MEMBER)
                    {
                        clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_adr, &adr_sz);

                        if (clnt_sock == -1) //어떤 이류로 인한 연결 실패
                        {
                            member_num--;
                            printf("accept() error\n");
                            i--;
                            continue;
                        }
                        else
                        {
                            FD_SET(clnt_sock, &reads);
                            if (fd_max < clnt_sock)
                                fd_max = clnt_sock;
                            new_member(clnt_sock);
                            strcpy(buf.message, "**********환영합니다! 사용하실 이름을 적어주세요.***********");
                            send_message(buf, SYSTEM_MESSAGE, clnt_sock);
                            printf("<사용자 연결>: %d\n", clnt_sock);
                        }
                    }
                }
                else
                {
                    int read_result = receive_message(&buf, i);
                    if (read_result == -1)
                    {
                        printf("받는데 실패했습니다.\n");
                        continue;
                    }
                    printf("From client : %s\n", buf.message);
                    if (read_result == 0 || !strcmp(buf.message, "/end")) //연결을 끊었을 때
                    {
                        if (member_list[i].valid == TRUE) //이것은 유저가 나갔을 때만 다루어져야한다.
                        {
                            out_room(buf, &reads, i, fd_max);
                        }
                    }

                    //연결 요청 외의 것들을 다루는 곳
                    else
                    {
                        //*************************************************************************************************************************
                        //초기에 방설정과 이름을 정하는 함수 시작
                        if (member_list[i].valid == FALSE) //파이프로 부터 온 신호
                        {
                            if (buf.play == TRUE)
                            {
                                change_day(i, buf, &reads);
                                buf.play = FALSE;
                            }
                        }
                        else //유저로 부터 온 신호
                        {
                            if (member_list[i].play == TRUE)
                            { //마피아 게임하는 사람들의 채팅
                                mafia_chat(i, fd_max, buf, &reads);
                            }
                            else //마피아 게임 외의 채팅
                            {
                                if (member_list[i].first == EMPTY)
                                {
                                    first_enter(buf, i, fd_max);
                                }
                                else if (member_list[i].room == EMPTY)
                                {
                                    first_room(buf, i, fd_max);
                                }
                                //*************************************************************************************************************************
                                //초기에 방설정과 이름을 정하는 함수끝
                                else
                                {
                                    message_task(buf, i, &fd_max, &reads);
                                }
                            }
                        }
                    }
                } //else 괄호
            }
        } //이까지가 select for문이다.

    } //while문 닫는 괄호void send_message(member buf);
} //main끝

/**************************************** 마피아 코드 *******************************************************/
//마피아 게임

void mafia_chat(int i, int fd_max, member buf, fd_set *reads)
{                      //일단 만들어 놓음 이건 마피아게임일 하는 방이면 이 채팅기법으로 넘어온다.
    int room_pos = -1; //room_pos는 마피아게임을 할 때
    for (int j = 0; j < room_max; j++)
    {
        if (room_mafia[j].room_number == member_list[i].room)
        {
            room_pos = j; //사용자들의 마피아게임방이 어딘지 위치를 얻어낸다.(get room information playing mafiagame)
        }
    }
    if (room_pos == -1)
    {
        //디버깅용
        strcpy(buf.message, "소속된 방이 존재하지 않습니다.\n");
        send_message(buf, SYSTEM_MESSAGE, i);
        return;
    }
    if (room_mafia[room_pos].day == noon)
    { //현석
        if (buf.type == SYSTEM_MESSAGE)
        {
            strcpy(buf.message, "마피아 게임중에는 메세지를 허용하지 않습니다."); //귓속말, 차단 기능 전부 무시, 나가기만 가능
            send_message(buf, SYSTEM_MESSAGE, i);
        }
        else
        {
            if (member_list[i].live == dead)
            { //죽은 자는 말을 하지 못한다.
                return;
            }
            strcpy(buf.name, member_list[i].name);
            mafia_send_message(buf, USER_MESSAGE, room_pos); //일상적인 대화는 그대로 날린다.
        }
    }
    else if (room_mafia[room_pos].day == vote)
    { //투표시
        char name[MAX_NAME_SIZE];

        if (member_list[i].live == dead)
        { //죽은 자는 투표못함
            return;
        }
        if (buf.type == USER_MESSAGE)
        {
            strcpy(buf.message, "/이름 으로 투표해주세요.");
            send_message(buf, SYSTEM_MESSAGE, i);
            return;
        }
        if (member_list[i].skill == use)
        {
            strcpy(buf.message, "이미 투표를 하셨습니다.");
            send_message(buf, SYSTEM_MESSAGE, i);
            return;
        }
        strcpy(name, buf.message + 1);
        for (int j = 0; j < room_mafia[room_pos].mem_number; j++)
        {
            if (room_mafia[room_pos].member_list[j] != EMPTY && member_list[room_mafia[room_pos].member_list[j]].live == alive)
            {
                if (!strcmp(member_list[room_mafia[room_pos].member_list[j]].name, name)) //같은 이름을 올려야함
                {
                    member_list[room_mafia[room_pos].member_list[j]].vote_num++;
                    member_list[i].skill = use;
                    strcpy(buf.message, "<");
                    strcat(buf.message, name);
                    strcat(buf.message, ">님을 투표했습니다.");
                    send_message(buf, SYSTEM_MESSAGE, i);
                    return;
                }
            }
        }
        strcpy(buf.message, "존재하지 않거나 죽은 사용자입니다.");
        send_message(buf, SYSTEM_MESSAGE, i);
    }
    else if (room_mafia[room_pos].day == night)
    { //밤에 능력을 쓸 시
        //시민과 군인은 채팅 불가
        if (member_list[i].live == dead)
        {
            return;
        }
        char name[MAX_NAME_SIZE];
        switch (member_list[i].job)
        {
        case citizen:
        case soldier:
            return;
        case police:
            if (buf.type == USER_MESSAGE)
            {
                strcpy(buf.message, "/이름 으로 확인해주세요.");
                send_message(buf, SYSTEM_MESSAGE, i);
                return;
            }
            if (member_list[i].skill == use)
            {
                strcpy(buf.message, "능력은 한번만 사용할 수 있습니다.");
                send_message(buf, SYSTEM_MESSAGE, i);
                return;
            }
            strcpy(name, buf.message + 1);
            for (int j = 0; j < room_mafia[room_pos].mem_number; j++)
            {
                if (room_mafia[room_pos].member_list[j] != EMPTY && member_list[room_mafia[room_pos].member_list[j]].live == alive)
                {
                    if (!strcmp(member_list[room_mafia[room_pos].member_list[j]].name, name))
                    { //같은 이름이 있을 때 능력을 발동시킨다.
                        if (member_list[room_mafia[room_pos].member_list[j]].job == mafia)
                        {
                            strcpy(buf.message, "<");
                            strcat(buf.message, member_list[room_mafia[room_pos].member_list[j]].name);
                            strcat(buf.message, ">은 마피아입니다.");
                            send_message(buf, SYSTEM_MESSAGE, i);
                            member_list[i].skill = use;
                            return;
                        }
                        else
                        {
                            strcpy(buf.message, "<");
                            strcat(buf.message, member_list[room_mafia[room_pos].member_list[j]].name);
                            strcat(buf.message, ">은 마피아가 아닙니다.");
                            send_message(buf, SYSTEM_MESSAGE, i);
                            member_list[i].skill = use;
                            return;
                        }
                    }
                }
            }
            strcpy(buf.message, "존재하지 않거나 죽은 사용자입니다.");
            send_message(buf, SYSTEM_MESSAGE, i);
            //경찰 끝
            break;
        case docter:
            if (member_list[i].skill == use)
            {
                strcpy(buf.message, "이미 한명을 지정했습니다.");
                send_message(buf, SYSTEM_MESSAGE, i);
                return;
            }
            strcpy(name, buf.message + 1);
            for (int j = 0; j < room_mafia[room_pos].mem_number; j++)
            {
                if (room_mafia[room_pos].member_list[j] != EMPTY && member_list[room_mafia[room_pos].member_list[j]].live == alive)
                {
                    if (!strcmp(member_list[room_mafia[room_pos].member_list[j]].name, name))
                    {                                                                      //같은 이름이 있을 때 능력을 발동시킨다.
                        member_list[i].skill_target = room_mafia[room_pos].member_list[j]; //살릴 사람을 결정한다.
                        strcpy(buf.message, "<");
                        strcat(buf.message, member_list[room_mafia[room_pos].member_list[j]].name);
                        strcat(buf.message, ">님을 치료합니다. ");
                        send_message(buf, SYSTEM_MESSAGE, i);
                        member_list[i].skill = use;
                        return;
                    }
                }
            }
            strcpy(buf.message, "존재하지 않거나 죽은 사용자입니다.");
            send_message(buf, SYSTEM_MESSAGE, i);
            break; //의사끝
        case mafia:
            if (buf.type == USER_MESSAGE)
            {
                for (int j = 0; j < room_mafia[room_pos].mem_number; j++)
                {
                    if (room_mafia[room_pos].member_list[j] != EMPTY) //나간 사용자면 안된다.
                    {
                        if (member_list[room_mafia[room_pos].member_list[j]].job == mafia) //마피아끼리는 말할 수 있음
                        {                                                                  //죽은자도 들을 수 있다.
                            strcpy(buf.name, member_list[i].name);
                            send_message(buf, USER_MESSAGE, room_mafia[room_pos].member_list[j]);
                        }
                    }
                }
                return;
            }
            else if (buf.type == SYSTEM_MESSAGE)
            {
                if (member_list[i].skill == use)
                {
                    strcpy(buf.message, "이미 한명을 지정했습니다.");
                    send_message(buf, SYSTEM_MESSAGE, i);
                    return;
                }
                strcpy(name, buf.message + 1); //이름 저장
                for (int j = 0; j < room_mafia[room_pos].mem_number; j++)
                {
                    if (room_mafia[room_pos].member_list[j] != EMPTY) //나간사용자면 검사할 필요 없다.
                    {
                        if (!strcmp(name, member_list[room_mafia[room_pos].member_list[j]].name) && member_list[room_mafia[room_pos].member_list[j]].live == alive)
                        {
                            for (int k = 0; k < room_mafia[room_pos].mem_number; k++)
                            {
                                if (room_mafia[room_pos].member_list[k] != EMPTY && member_list[room_mafia[room_pos].member_list[k]].job == mafia)
                                {
                                    member_list[room_mafia[room_pos].member_list[k]].skill = use;
                                    member_list[room_mafia[room_pos].member_list[k]].skill_target = room_mafia[room_pos].member_list[j];
                                    strcpy(buf.message, "<");
                                    strcat(buf.message, member_list[room_mafia[room_pos].member_list[j]].name);
                                    strcat(buf.message, ">를 목표로 지정했습니다.");
                                    send_message(buf, SYSTEM_MESSAGE, room_mafia[room_pos].member_list[k]);
                                }
                            }
                            return;
                        }
                    }
                }
            }
            strcpy(buf.message, "존재하지 않거나 죽은 사용자입니다.");
            send_message(buf, SYSTEM_MESSAGE, i);
            break;
        default:
            return;
        }
    }
    else
    {
        error_message("에러가 발생했습니다.");
        end_mafia_game(room_mafia[room_pos].to_main_pipe[0], room_pos, reads);
    }
}

void result_vote(int room_pos)
{ //투표결과에 따라 멤버를 죽은지 살은지를 설정하는 함수
    member buf;
    int vote_number = 0;
    int most_voted_index = -1;
    int most_votes = 0;
    int draw = FALSE;

    if (room_mafia[room_pos].out_member == TRUE)
    { //투표도중 나간사람이 있는 경우
        strcpy(buf.message, "투표도중 나간 사람이 있으므로 투표를 무효합니다");
        mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
    }
    else
    { //투표가 끝날때 까지 나간사람이 없는 경우
        for (int j = 0; j < room_mafia[room_pos].mem_number; j++)
        {
            if (room_mafia[room_pos].member_list[j] != EMPTY && member_list[room_mafia[room_pos].member_list[j]].live == alive)
            { //나간 사람이 아닐때 살아 있을 때만 그 소켓을 읽어야한다.
                if (most_votes < member_list[room_mafia[room_pos].member_list[j]].vote_num)
                {
                    draw = FALSE; //많은 사람이 나타는 순간 비김을 비활성화 시킨다.
                    most_votes = member_list[room_mafia[room_pos].member_list[j]].vote_num;
                    most_voted_index = room_mafia[room_pos].member_list[j]; //가장 많은 투표를 받은 사람
                }
                else if (most_votes == member_list[room_mafia[room_pos].member_list[j]].vote_num)
                { //투표동률이 있을경우는 비김을 활성화 시킨다.
                    draw = TRUE;
                }
            }
        }
        if (draw == TRUE)
        { //비긴 경우
            sprintf(buf.message, "%d", most_votes);
            strcat(buf.message, "표로 투표가 비겼습니다.");
            mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
            strcpy(buf.message, "비긴 사람 : ");
            for (int j = 0; j < room_mafia[room_pos].mem_number; j++)
            {
                if (room_mafia[room_pos].member_list[j] != EMPTY && member_list[room_mafia[room_pos].member_list[j]].live == alive)
                { //아직 게임을 하는 사람이면 사라있는 사람이면
                    if (member_list[room_mafia[room_pos].member_list[j]].vote_num == most_votes)
                    {
                        strcat(buf.message, member_list[room_mafia[room_pos].member_list[j]].name);
                        strcat(buf.message, " ");
                    }
                }
            }
            strcat(buf.message, "입니다.");
            mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
        }
        else
        {
            member_list[most_voted_index].live = dead;
            strcpy(buf.message, member_list[most_voted_index].name);
            strcat(buf.message, "님이 투표로 죽었습니다.");
            mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
        }
    }
    for (int j = 0; j < room_mafia[room_pos].mem_number; j++)
    {
        if (room_mafia[room_pos].member_list[j] != EMPTY && member_list[room_mafia[room_pos].member_list[j]].live == alive)
        {                                                                   //EMPTY는 나갔다는 뜻이다.
            member_list[room_mafia[room_pos].member_list[j]].vote_num = 0;  //투표초기화
            member_list[room_mafia[room_pos].member_list[j]].skill = unuse; //능력을 안썼따.
            if (member_list[room_mafia[room_pos].member_list[j]].job != soldier)
            {
                member_list[room_mafia[room_pos].member_list[j]].skill_target = FALSE;
            }
        }
    }
    //세팅을 초기화해야한다.
}

void result_night(int room_pos)
{   //능력에 따라서 화면에 출력하고 죽일지 살릴지 설정하는 함수
    //마피아, 경찰 능력 사용
    member buf;
    char mafia_target = EMPTY;
    char docter_target = EMPTY;
    for (int i = 0; i < room_mafia[room_pos].mem_number; i++)
    {
        if (room_mafia[room_pos].member_list[i] != EMPTY) //안나간지만 하면됨 지금부터 죽일거니까
        {
            jobs job = member_list[room_mafia[room_pos].member_list[i]].job; // 스킬 사용자의 직업
            if (job == mafia)
            {
                mafia_target = member_list[room_mafia[room_pos].member_list[i]].skill_target; // 타겟의 직업
            }
            else if (job == docter)
            {
                docter_target = member_list[room_mafia[room_pos].member_list[i]].skill_target;
            }
        }
    }
    for (int i = 0; i < room_mafia[room_pos].mem_number; i++)
    {
        if (room_mafia[room_pos].member_list[i] != EMPTY && mafia_target == room_mafia[room_pos].member_list[i])
        {   //나머지는 굳이 할필요없음 마피아 타겟일때만 하면 된다.
            //나간사람이 아니여야하고 마피아의 타겟이여야한다.
            if (docter_target == mafia_target)
            { //의사에 의해 산다.
                strcpy(buf.message, "<");
                strcat(buf.message, member_list[mafia_target].name);
                strcat(buf.message, ">님이 의사에 치료를 받고 살았습니다.");
                mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
                break;
            }
            else if (member_list[mafia_target].job == soldier && member_list[mafia_target].skill_target == EMPTY)
            {
                member_list[mafia_target].skill_target = FULL; //능력을 사용함
                strcpy(buf.message, "<");
                strcat(buf.message, member_list[mafia_target].name);
                strcat(buf.message, ">님이 복문신조 정신으로 살았습니다.");
                mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
                break;
            }
            else
            {
                member_list[mafia_target].live = dead;
                strcpy(buf.message, "<");
                strcat(buf.message, member_list[mafia_target].name);
                strcat(buf.message, ">님이 마피아의 공격으로 죽었습니다.");
                mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
                break;
            }
        }
    }

    for (int j = 0; j < room_mafia[room_pos].mem_number; j++)
    {
        if (room_mafia[room_pos].member_list[j] != EMPTY && member_list[room_mafia[room_pos].member_list[j]].live == alive)
        {
            member_list[room_mafia[room_pos].member_list[j]].skill = unuse;
            if (member_list[room_mafia[room_pos].member_list[j]].job != soldier)
            {
                member_list[room_mafia[room_pos].member_list[j]].skill_target = FALSE;
            }
        }
    } //세팅을 초기화해야한다.
}

void change_day(int i, member buf, fd_set *reads)
{   // i는 메인에게 날짜를 바꾸라고 보낸 child 프로세스입니다.
    //int j;
    int mafia_num = 0;
    int citizen_num = 0;
    int room_pos = buf.room;

    if (!strcmp(buf.message, "noon"))
    { //낮으로 바꾸어라
        room_mafia[room_pos].day = noon;
        result_night(room_pos);
        room_mafia[room_pos].out_member = FALSE; //나간사람이 없다고 다시 초기화 시킨다.
        mafia_number(buf.room, &room_mafia[room_pos].mafia_number, &room_mafia[room_pos].citizen_number);
        buf.mafia_num = room_mafia[room_pos].mafia_number;
        buf.citizen_num = room_mafia[room_pos].citizen_number;
        send_message(buf, SYSTEM_MESSAGE, room_mafia[room_pos].to_child_pipe[1]); //보내는건 1번으로
        strcpy(buf.message, "낮이 되었습니다.");
        mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
    }
    else if (!strcmp(buf.message, "vote"))
    { //투표모드로 바뀌는 코드
        room_mafia[room_pos].day = vote;

        room_mafia[room_pos].out_member = FALSE; //나간사람이 없다고 다시 초기화 시킨다.
        mafia_number(buf.room, &room_mafia[room_pos].mafia_number, &room_mafia[room_pos].citizen_number);
        buf.mafia_num = room_mafia[room_pos].mafia_number;
        buf.citizen_num = room_mafia[room_pos].citizen_number;
        send_message(buf, SYSTEM_MESSAGE, room_mafia[room_pos].to_child_pipe[1]); //보내는건 1번으로
        strcpy(buf.message, "투표를 시작합니다.");
        mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
        strcpy(buf.message, "생존자 리스트");
        mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
        strcpy(buf.message, "< ");
        for (int j = 0; j < room_mafia[room_pos].mem_number; j++)
        {
            if (room_mafia[room_pos].member_list[j] != EMPTY && member_list[room_mafia[room_pos].member_list[j]].live == alive)
            {
                strcat(buf.message, member_list[room_mafia[room_pos].member_list[j]].name);
                strcat(buf.message, " ");
            }
        }
        strcat(buf.message, ">입니다. /이름 으로 투표해주세요.");
        mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
    }
    else if (!strcmp(buf.message, "night"))
    { //밤으로 바꾸어라
        room_mafia[room_pos].day = night;

        result_vote(room_pos);
        room_mafia[room_pos].out_member = FALSE; //나간사람이 없다고 다시 초기화 시킨다.
        mafia_number(buf.room, &room_mafia[room_pos].mafia_number, &room_mafia[room_pos].citizen_number);
        buf.mafia_num = room_mafia[room_pos].mafia_number;
        buf.citizen_num = room_mafia[room_pos].citizen_number;
        send_message(buf, SYSTEM_MESSAGE, room_mafia[room_pos].to_child_pipe[1]); //보내는건 1번으로
        strcpy(buf.message, "밤이 되었습니다.");
        mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
    }
    else if (!strcmp(buf.message, "cw"))
    {
        strcpy(buf.message, "시민이 승리하였습니다.");
        mafia_send_message(buf, MAFIA_END_MESSAGE, room_pos);
        end_mafia_game(i, room_pos, reads);
    }
    else if (!strcmp(buf.message, "mw"))
    {
        strcpy(buf.message, "마피아가 승리하였습니다.");
        mafia_send_message(buf, MAFIA_END_MESSAGE, room_pos);
        end_mafia_game(i, room_pos, reads);
    }
    else
    {
        printf("보낸게 이상하다. <%s> \n", buf.message);
        strcpy(buf.message, "마피아 게임이 잘못된 명령어로 종료되었습니다.");
        mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
        end_mafia_game(i, room_pos, reads);
    }
}

void end_mafia_game(int i, int room_pos, fd_set *reads)
{
    member buf;
    close(i);
    close(room_mafia[room_pos].to_child_pipe[1]);
    FD_CLR(i, reads);
    FD_CLR(room_mafia[room_pos].to_child_pipe[1], reads);

    if (member_list[i].type == PIPE)
    {
        member_list[i].type = NO_PIPE;
        printf("파이프 [%d]를 해제합니다.\n", i);
    }
    if (member_list[room_mafia[room_pos].to_child_pipe[1]].type == PIPE)
    {
        member_list[room_mafia[room_pos].to_child_pipe[1]].type = NO_PIPE;
        printf("파이프 [%d]를 해제합니다.\n", room_mafia[room_pos].to_child_pipe[1]);
    }

    int mem_num = room_mafia[room_pos].mem_number;
    for (int j = 0; j < mem_num; j++)
    {
        if (room_mafia[room_pos].member_list[j] != EMPTY)
        {                                                                  //멤버상황을 리셋해주는 것
            member_list[room_mafia[room_pos].member_list[j]].play = FALSE; //다시 false로 해준다.
        }
    }

    strcpy(buf.message, "게임을 종료합니다.");
    mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
    memset(&room_mafia[room_pos], EMPTY, sizeof(room_mafia[room_pos])); //여기서 startgame도 FALSE가 되어버린다.
}

int mafia_number(int room_pos, int *mafia_num, int *live_num)
{ //확인완료
    int mem_number = room_mafia[room_pos].mem_number;
    *mafia_num = 0;
    *live_num = 0;
    for (int i = 0; i < mem_number; i++)
    {
        if (room_mafia[room_pos].member_list[i] != EMPTY && member_list[room_mafia[room_pos].member_list[i]].live == alive) //나갔는지 확인
        {                                                                                                                   //안나갔고 살아있을때만 인원으로 세야한다.
            if (member_list[room_mafia[room_pos].member_list[i]].job == mafia)
            {
                (*mafia_num)++;
            }
            else
            {
                (*live_num)++;
            }
        }
    }
    return 0;
}
void *mafia_game(void *args)
{
    int fd_max = (*(struct arg *)args).fd_max;
    int room_pos = (*(struct arg *)args).room_number;
    int live_number = (*(struct arg *)args).mem_number;
    int to_parent = room_mafia[room_pos].to_main_pipe[1];
    int from_parent = room_mafia[room_pos].to_child_pipe[0];

    for (int i = 3; i < fd_max + 1; i++)
    { //소켓은 3번부터
        if (i != to_parent && i != from_parent)
        {             //부모와 다다르면
            close(i); //다 닫아준다.
        }
    }
    int mafia_num;
    member buf;
    memset(&buf, EMPTY, sizeof(buf));
    today day = night;
    while (1)
    {
        signal(SIGALRM, SIG_IGN);
        alarm(30); //30초마다 바뀐다.
        sleep(30);
        if (day == night) //바뀌는 순서는 밤 -> 낮 -> 투표 -> 밤 //change order night-> noon-> vote -> night
        {                 //밤-> 낮
            strcpy(buf.message, "noon");
            day = noon;
        }
        else if (day == noon)
        {
            strcpy(buf.message, "vote");
            day = vote;
        }
        else
        { //낮->밤
            strcpy(buf.message, "night");
            day = night;
        }
        buf.play = TRUE;
        buf.room = room_pos; //몇번방인지 항상 알려준다.
        send_message(buf, SYSTEM_MESSAGE, to_parent);
        if (receive_message(&buf, from_parent) == 0)
        { //항상 날짜를 바꾸고 검사한다.
            printf("접속을 종료합니다.\n");
            close(from_parent);
            close(to_parent);
            exit(0);
        }
        if (buf.mafia_num >= buf.citizen_num || buf.mafia_num == 0)
        {
            break;
        }
    }
    if (buf.mafia_num == 0)
    {
        strcpy(buf.message, "cw");
        send_message(buf, SYSTEM_MESSAGE, to_parent);
    }
    else
    {
        strcpy(buf.message, "mw");
        send_message(buf, SYSTEM_MESSAGE, to_parent);
    }

    receive_message(&buf, from_parent);
    close(from_parent);
    close(to_parent);
    exit(0);
    printf("-----------종료가 안됨 에러감지------------");
    return NULL;
}

int initial_game(int mem_number, int room_pos) //방초기화 과정
{
    jobs job[12];
    jobs temp;
    room_mafia[room_pos].mem_number = mem_number;
    room_mafia[room_pos].day = night;

    int j;
    for (int i = 0; i < mem_number; i++)
    {
        job[i] = citizen;
    }
    if (mem_number <= 5)
    { //4명일 때            마피아 : 2, 경찰 : 1
        job[0] = mafia;
        job[1] = police;
    }
    else if (mem_number <= 7)
    { //5명에서 6명일 때    마피아 : 2, 경찰 : 1, 의사 : 1
        job[0] = mafia;
        job[1] = mafia;
        job[2] = police;
        job[3] = docter;
    }
    else if (mem_number < 10)
    { //7명에서 9명일때     마피아 : 3, 경찰 : 1, 의사 : 1, 군인 : 1
        job[0] = mafia;
        job[1] = mafia;
        job[2] = mafia;
        job[3] = police;
        job[4] = docter;
        job[5] = soldier;
    }
    else
    { //10명에서 12명일 때   마피아 : 3, 경찰 : 1, 의사 : 1, 군인 : 2
        job[0] = mafia;
        job[1] = mafia;
        job[2] = mafia;
        job[3] = police;
        job[4] = docter;
        job[5] = soldier;
        job[6] = soldier;
    }
    for (int i = 0; i < mem_number; i++)
    {
        j = rand() % mem_number;
        SWAP(job[i], job[j], temp);
    }
    //섞었다.
    for (int i = 0; i < mem_number; i++)
    {
        member_list[room_mafia[room_pos].member_list[i]].play = TRUE;          //게임진행중 초기화
        member_list[room_mafia[room_pos].member_list[i]].job = job[i];         //직업설정
        member_list[room_mafia[room_pos].member_list[i]].skill = unuse;        //능력도 다 초기화 해준다.
        member_list[room_mafia[room_pos].member_list[i]].skill_target = EMPTY; //능력도 다 초기화 해준다.
        member_list[room_mafia[room_pos].member_list[i]].live = alive;         //아직 살아있음을 해줌
    }
    return 0;
}

//마피아 게임 시작
int start_mafia(int i, int *fd_max, fd_set *reads)
{ //이미 게임중인지 확인했고 인원은 4명에서 12명사이다.
    int mem_number = 0;
    member buf;
    int temp_member[12]; //최대 열두명이니까
    int error = FALSE;
    int room_pos = 0;

    for (int j = 0; j < *fd_max + 1; j++)
    {
        if (member_list[j].valid == TRUE && member_list[j].room == member_list[i].room)
        { //i를 포함해서 다넣는다.
            if ((mem_number) >= 12)
            {
                error = TRUE;
                break;
            }
            temp_member[mem_number] = j;
            (mem_number)++;
        }
    }
    if ((mem_number) <= 3 || error)
    {
        error = TRUE;
        return error;
    }
    //지금 부터 만든다.
    room_pos = for_mafia_room(i); //마피아하는 방들은 따로 모아서 관리할 것이다.
    printf("<%d>\n", mem_number);
    for (int j = 0; j < *fd_max + 1; j++)
    {
        room_mafia[room_pos].member_list[j] = temp_member[j]; //소켓을 전부 등록해주는 과정
    }
    if (initial_game(mem_number, room_pos))
    {
        error_message("직업 설정 실패");
        return -1;
    }
    struct arg args;
    args.room_number = room_pos;
    args.mem_number = mem_number;
    args.fd_max = *fd_max;

    room_mafia[room_pos].startgame = TRUE; //직업설정까지 다끝나면 정상적으로 해야하니까 마지막에 넣어준다.
    for (int j = 0; j < mem_number; j++)
    {
        printf("%s\n", member_list[temp_member[j]].name);
    }
    room_mafia[room_pos].room_number = member_list[i].room; //어느방에서 하고 있는지 가르쳐준다.
    if (pipe(room_mafia[room_pos].to_main_pipe) != 0)
    {
        error_message("파이프생성실패");
    }
    if (pipe(room_mafia[room_pos].to_child_pipe) != 0)
    {
        error_message("파이프생성실패");
    }
    if (fork() == 0)
    { //마피아 게임은 multi process개념으로 한다. (sleep적용때문에)
        mafia_game(&args);
    }

    //마피아 게임이랑 파이프 연결 끝
    close(room_mafia[room_pos].to_main_pipe[1]);
    close(room_mafia[room_pos].to_child_pipe[0]);
    FD_SET(room_mafia[room_pos].to_main_pipe[0], reads);
    FD_SET(room_mafia[room_pos].to_child_pipe[1], reads);          //이걸등록해야 나중에 같은 디스크립터 번호로 소켓에 안보낸다.
    member_list[room_mafia[room_pos].to_main_pipe[0]].type = PIPE; //파이프임을 알려준다.  디버깅용
    member_list[room_mafia[room_pos].to_child_pipe[1]].type = PIPE;
    if (*fd_max < room_mafia[room_pos].to_child_pipe[1])
    {
        *fd_max = room_mafia[room_pos].to_child_pipe[1];
    }

    strcpy(buf.message, "*********<마피아 게임을 시작합니다.>*********\n");
    mafia_send_message(buf, MAFIA_MESSAGE, room_pos); //마피아 시작 시 채팅 화면을 clear하기 위해 MAFIA_MESSAGE 사용
    room_mafia[room_pos].day = night;
    strcpy(buf.message, "밤이 되었습니다.");
    mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
    strcpy(buf.message, "생존자 리스트");
    mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);
    strcpy(buf.message, "< ");
    for (int j = 0; j < room_mafia[room_pos].mem_number; j++)
    {
        if (room_mafia[room_pos].member_list[j] != EMPTY && member_list[room_mafia[room_pos].member_list[j]].live == alive)
        {
            strcat(buf.message, member_list[room_mafia[room_pos].member_list[j]].name);
            strcat(buf.message, " ");
        }
    }
    strcat(buf.message, ">입니다. /이름 으로 경찰, 마피아, 의사는 능력을 쓸 수 있습니다.");
    mafia_send_message(buf, SYSTEM_MESSAGE, room_pos);

    jobs job;

    for (int j = 0; j < mem_number; j++)
    {
        job = member_list[room_mafia[room_pos].member_list[j]].job;
        switch (job)
        {
        case mafia:
            strcpy(buf.message, "당신은 마피아입니다. 죽일 사용자의 이름을 /이름 형식으로 입력해주세요.");
            send_message(buf, SYSTEM_MESSAGE, room_mafia[room_pos].member_list[j]);
            break;
        case citizen:
            strcpy(buf.message, "당신은 시민입니다.");
            send_message(buf, SYSTEM_MESSAGE, room_mafia[room_pos].member_list[j]);
            break;
        case police:
            strcpy(buf.message, "당신은 경찰입니다. 확인해볼 사용자의 이름을 /이름 형식으로 입력해주세요.");
            send_message(buf, SYSTEM_MESSAGE, room_mafia[room_pos].member_list[j]);
            break;
        case docter:
            strcpy(buf.message, "당신은 의사입니다. 살릴 사용자의 이름을 /이름 형식으로 입력해주세요.");
            send_message(buf, SYSTEM_MESSAGE, room_mafia[room_pos].member_list[j]);
            break;
        case soldier:
            strcpy(buf.message, "당신은 군인입니다. 마피아의 공격을 한번 버틸 수 있습니다.");
            send_message(buf, SYSTEM_MESSAGE, room_mafia[room_pos].member_list[j]);
            break;
        default:
            printf("당신은 누구십니까?");
        }
    }

    return 0; //마피아게임을 만들었다.
}

int for_mafia_room(int i)
{ //빈방을 찾아준다.
    int j;
    for (j = 0; j <= room_max; j++)
    {
        if (room_mafia[j].startgame == FALSE)
        {
            break;
        }
    }
    if (j >= room_max)
    {
        room_max = j + 1;
    }

    return j; //빈방을 넘겨준다.
}

void mafia_send_message(member buf, char type, int room_pos)
{
    int mem_num = room_mafia[room_pos].mem_number;
    for (int i = 0; i < mem_num; i++)
    {
        if (room_mafia[room_pos].member_list[i] != EMPTY) //나간 사람이 아니면
        {                                                 //타당한 유저면 다 보낸다.
            send_message(buf, type, room_mafia[room_pos].member_list[i]);
        }
    }
}

/**************************************** 마피아 코드 *******************************************************/

void out_room(member buf, fd_set *reads, int i, int fd_max)
{
    FD_CLR(i, reads);
    char name[MAX_NAME_SIZE];
    int room = member_list[i].room;

    strcpy(buf.message, "----------------------------------------------------------------------------------------------------------");
    send_message(buf, FREE_MESSAGE, i);
    strcpy(buf.message, "다음에 뵐게요!");
    send_message(buf, SYSTEM_MESSAGE, i);

    if (member_list[i].play == TRUE) //게임 도중에 나간경우
    {
        for (int j = 0; j < room_max; j++)
        {
            if (room_mafia[j].room_number == room)
            {                                    //같은 방을 찾는다.
                room_mafia[j].out_member = TRUE; //나간사람이 있다고 알려준다.   나중에 투표시 사용할 것임
                for (int k = 0; k < room_mafia[j].mem_number; k++)
                {
                    if (room_mafia[j].member_list[k] == i)
                    { //나간 사람이라고 EMPTY처리해준다.
                        room_mafia[j].member_list[k] = EMPTY;
                        break;
                    }
                }
                break;
            }
        }
    }

    strcpy(name, member_list[i].name);
    memset(&member_list[i], EMPTY, sizeof(member_list[i]));                              //전부 0으로 초기화시킨다.
    memset(blocking_list[i].block_member, FALSE, sizeof(blocking_list[i].block_member)); //차단정보 초기화
    close(i);
    printf("closed client: %d \n", i);
    if (room != EMPTY)
    {
        strcpy(buf.message, "******< ");
        strcat(buf.message, name);
        strcat(buf.message, " > 님이 나가셨습니다.******");
        for (int j = 0; j < fd_max + 1; j++)
        {
            if (member_list[j].valid == TRUE && member_list[j].room == room)
            {
                send_message(buf, SYSTEM_MESSAGE, j);
            }
        }
    }
    member_num--;
}

void first_room(member buf, int i, int fd_max)
{
    char buf_temp[BUF_SIZE];
    member_list[i].room = atoi(buf.message);
    if (member_list[i].room == 0)
    {
        strcpy(buf.message, "0번방은 사용할 수 없습니다. 다시 입력해주세요.");
        send_message(buf, SYSTEM_MESSAGE, i);
        return;
    }
    for (int j = 0; j < room_max; j++)
    {
        if (room_mafia[j].room_number == member_list[i].room)
        {
            strcpy(buf_temp, "<");
            strcat(buf_temp, buf.message);
            strcat(buf_temp, ">방은 마피아 게임중이라서 들어갈 수 없습니다. 다시 입력해주세요. ");
            strcpy(buf.message, buf_temp);
            member_list[i].room = EMPTY;
            send_message(buf, SYSTEM_MESSAGE, i);
            return;
        }
    }
    strcpy(buf_temp, "< ");
    strcat(buf_temp, buf.message);
    strcat(buf_temp, " >");
    strcat(buf_temp, "방에 입장하셨습니다.");
    strcpy(buf.message, buf_temp);
    send_message(buf, SYSTEM_MESSAGE, i);

    strcpy(buf.message, "/end:종료, /w 이름:(귓속말), /b 이름:x차단x, /nb 이름:o차단해제o /start:마피아 게임 시작");
    send_message(buf, SYSTEM_MESSAGE, i);

    strcpy(buf.message, "----------------------------------------------------------------------------------------------------------");
    send_message(buf, FREE_MESSAGE, i);

    for (int j = 0; j < fd_max + 1; j++)
    {
        if (member_list[j].valid == TRUE && (member_list[j].room == member_list[i].room && i != j))
        {
            strcpy(buf.message, "*******방에 <");
            strcat(buf.message, member_list[i].name);
            strcat(buf.message, ">님이 입장하셨습니다.*******");
            send_message(buf, SYSTEM_MESSAGE, j);
        }
    }
}

void first_enter(member buf, int i, int fd_max)
{
    int room_check = 0;
    int room_list[MAX_ROOM];
    char buf_temp[BUF_SIZE]; //메세지 옮기기위한 변수
    if (strlen(buf.message) >= MAX_NAME_SIZE - 1)
    {
        strcpy(buf.message, "이름은 최대 20글자입니다.");
        send_message(buf, SYSTEM_MESSAGE, i);
        return;
    }
    else if (checking_name(buf, fd_max))
    {
        strcpy(member_list[i].name, buf.message);
        strcpy(buf.message, "이름이 <");
        strcat(buf.message, member_list[i].name);
        strcat(buf.message, ">으로 설정이 완료되었습니다.");
        printf("buf_message : %s \n", buf.message);
        send_message(buf, SYSTEM_MESSAGE, i);
    }
    else
    {
        strcpy(buf.message, "중복되는 이름이 있습니다. 다른이름을 선택해주세요.");
        send_message(buf, SYSTEM_MESSAGE, i);
        return;
    }

    for (int j = 0; j < fd_max + 1; j++)
    {
        if (member_list[j].room != EMPTY)
        {
            room_check = 1; //이건 필요함
        }
    }
    if (room_check)
    {
        strcpy(buf.message, "현재 생성되어있는 방목록은 다음과 같습니다.");
        buf.type = SYSTEM_MESSAGE;
        send_message(buf, SYSTEM_MESSAGE, i);
        memset((void *)room_list, 0, sizeof(int) * MAX_ROOM);
        int fill_num = 0;
        for (int j = 0; j < fd_max + 1; j++)
        {
            if (member_list[j].room != EMPTY)
            {

                if (alreay_print_room(room_list, member_list[j].room, fill_num))
                {
                    strcpy(buf.message, "< 방번호: ");
                    sprintf(buf_temp, "%d", member_list[j].room);
                    strcat(buf.message, buf_temp);
                    strcat(buf.message, " >\n");
                    room_list[fill_num++] = member_list[j].room;
                    buf.type = FREE_MESSAGE;
                    send_message(buf, SYSTEM_MESSAGE, i);
                }
            }
        }

        //다시 확인하기 위해 초기화시키는 코드
        strcpy(buf.message, "입장하실 방을 입력하시거나 또는 새로운 방번호를 입력해주세요.");
        send_message(buf, SYSTEM_MESSAGE, i);
    }
    else
    {
        strcpy(buf.message, "방이 없습니다. 새로운 방번호를 입력해주세요.");
        send_message(buf, SYSTEM_MESSAGE, i);
    }
    member_list[i].first = FULL;
}

void message_task(member buf, int i, int *fd_max, fd_set *reads)
{
    //특수 이벤트 처리
    if (buf.type == SYSTEM_MESSAGE)
    {
        char *ptr = strtok(buf.message, " ");
        if (!strcmp(ptr, "/w")) //귓속말
        {
            int check = 0;
            ptr = strtok(NULL, " ");
            if (strlen(ptr) >= MAX_NAME_SIZE)
            {
                strcpy(buf.message, "이름은 20글자까지입니다.");
                send_message(buf, SYSTEM_MESSAGE, i);
            }
            else
            {
                char name[MAX_NAME_SIZE];
                strcpy(name, ptr);
                ptr = strtok(NULL, " ");
                for (int j = 0; j < *fd_max + 1; j++)
                {
                    if (member_list[j].valid == TRUE && (!strcmp(name, member_list[j].name) && j != i))
                    {
                        if (member_list[j].room == member_list[i].room)
                        {
                            strcpy(name, member_list[i].name);
                            check = 1;                                     //차단한지 알면 곤란하니까
                            if (blocking_list[j].block_member[i] == FALSE) //차단도 안당해있어야함
                            {
                                strcpy(buf.message, ptr);
                                strcpy(buf.name, name);
                                send_message(buf, WHISPER_MESSAGE, j);
                                send_message(buf, WHISPER_MESSAGE, i);
                                break;
                            }
                        }
                        break;
                    }
                }
                if (check == 0)
                {
                    strcpy(buf.message, "접속하지 않은 사용자입니다.");
                    send_message(buf, SYSTEM_MESSAGE, i);
                }
            }
        }
        else if (!strcmp("/b", ptr)) //차단
        {
            int check = 0;
            ptr = strtok(NULL, " ");
            char name[MAX_NAME_SIZE];
            if (strlen(ptr) >= MAX_NAME_SIZE)
            {
                strcpy(buf.message, "이름은 20글자까지입니다.");
                send_message(buf, SYSTEM_MESSAGE, i);
            }
            else
            {
                strcpy(name, ptr);
                for (int j = 0; j < *fd_max + 1; j++)
                {
                    if (member_list[j].valid == TRUE && (!strcmp(name, member_list[j].name) && j != i)) //pipe인지 확인해준다.
                    {

                        if (member_list[j].room == member_list[i].room)
                        {
                            check = 1;
                            if (blocking_list[i].block_member[j] == FALSE) //차단이 안된 대상만 차단한다.
                            {
                                check = 2;
                                blocking_list[i].block_member[j] = TRUE;
                                strcpy(buf.message, "[");
                                strcat(buf.message, name);
                                strcat(buf.message, "]님을 차단했습니다.");
                                send_message(buf, SYSTEM_MESSAGE, i);
                                break;
                            }
                        }
                        break;
                    }
                }
                if (check == 0)
                {
                    strcpy(buf.message, "접속하지 않은 사용자입니다.");
                    send_message(buf, SYSTEM_MESSAGE, i);
                }
                else if (check == 1)
                {
                    strcpy(buf.message, "이미 차단한 대상입니다.");
                    send_message(buf, SYSTEM_MESSAGE, i);
                }
            }
        }
        else if (!strcmp("/nb", ptr))
        {
            int check = 0;
            ptr = strtok(NULL, " ");
            char name[MAX_NAME_SIZE];
            if (strlen(ptr) >= MAX_NAME_SIZE)
            {
                strcpy(buf.message, "이름은 20글자까지입니다.");
                send_message(buf, SYSTEM_MESSAGE, i);
            }
            else
            {
                strcpy(name, ptr);
                for (int j = 0; j < *fd_max + 1; j++)
                {
                    if (member_list[j].valid == TRUE && (!strcmp(name, member_list[j].name) && j != i)) //pipe인지 확인해준다.
                    {
                        if (member_list[j].room == member_list[i].room)
                        {
                            check = 1;
                            if (blocking_list[i].block_member[j] == TRUE) //차단 되어 있을 때만 해제한다.
                            {
                                check = 2;
                                blocking_list[i].block_member[j] = FALSE;
                                strcpy(buf.message, "[");
                                strcat(buf.message, name);
                                strcat(buf.message, "]님을 차단해제했습니다.");
                                send_message(buf, SYSTEM_MESSAGE, i);
                                break;
                            }
                        }
                        break;
                    }
                }
                if (check == 0)
                {
                    strcpy(buf.message, "접속하지 않은 사용자입니다.");
                    send_message(buf, SYSTEM_MESSAGE, i);
                }
                else if (check == 1)
                {
                    strcpy(buf.message, "차단한 대상이 아닙니다.");
                    send_message(buf, SYSTEM_MESSAGE, i);
                }
            }
        }
        else if (!strcmp("/help", ptr))
        {
            strcpy(buf.message, "/end:종료, /w 이름:(귓속말), /b 이름:x차단x, /nb 이름:o차단해제o /start:마피아 게임 시작");
            send_message(buf, SYSTEM_MESSAGE, i);
        }
        else if (!strcmp("/start", buf.message))
        { //마피아 게임 시작
            if (member_list[i].play == TRUE)
            {
                strcpy(buf.message, "이미 게임을 하고 있습니다.");
                send_message(buf, SYSTEM_MESSAGE, i);
            }
            else
            {
                if (start_mafia(i, fd_max, reads))
                { //실패했을 경우
                    printf("방을 만드는데 실패했습니다.");
                    strcpy(buf.message, "최소 인원은 4명 최대인원은 12명입니다.");
                    send_message(buf, SYSTEM_MESSAGE, i);
                }
                else
                {
                    printf("마피아게임이 시작됩니다."); //청소 코드를 여기 넣을까 생각중
                }
            }
        }
    }
    //일반 채팅
    else
    {
        strcpy(buf.name, member_list[i].name);
        for (int j = 0; j < *fd_max + 1; j++)
        {
            if (member_list[j].valid == TRUE && member_list[i].room == member_list[j].room)
            {
                if (blocking_list[j].block_member[i] == FALSE) //차단안당했을 때만 보내기
                {
                    send_message(buf, USER_MESSAGE, j);
                }
            }
        }
    }
}

int checking_name(member buf, int fd_max)
{
    for (int j = 4; j < fd_max + 1; j++) //0,1,2,3 은 stdin,stdout,stderr,serv_sock라서 무시
    {
        if (member_list[j].valid == TRUE)
        {
            if (!strcmp(member_list[j].name, buf.message))
            {
                return 0;
            }
        }
    }
    return 1;
}

int receive_message(member *buf, int from)
{
    //printf("받는다.\n");
    int str_len = 0;
    int full_message = 0;

    while (full_message < sizeof(member))
    {
        str_len = read(from, (char *)buf + full_message, sizeof(member));
        if (str_len == 0 || str_len == -1)
        { //실패했거나 EOF를 받았거나
            return str_len;
        }
        full_message += str_len;
    }
    return full_message;
}

void send_message(member buf, char type, int dest)
{
    buf.type = type;
    write(dest, (char *)&buf, sizeof(member));
}

int alreay_print_room(int *room_list, int room_num, int fill_num)
{
    for (int i = 0; i < fill_num; i++)
    {
        if (room_num == room_list[i])
        {
            return 0;
        }
    }
    return 1;
}
//멤버를 등록하는 함수
void new_member(int num)
{
    member_list[num].valid = TRUE; //valid는 파이프와 멤버를 구분하는 아주 중요한 변수이다.
}

void error_handling(char *buf)
{
    fputs(buf, stderr);
    fputc('\n', stderr);
    exit(1);
}
