#include "protocol_header.h"
#include "cJSON.h"

int chat_socks[100];
int chat_cnt = 0;
pthread_mutex_t mutx; // 뮤텍스 선언
ChatRoom room_list[MAX_ROOMS];
// 함수 프로토타입 선언
int is_user_exists(const char *user_id);                                            // 중복 회원가입 막기
void error_handling(char *message);                                                 // 에러 막기 로직
void save_user_data(User *user);                                                    // 회원정보를 JSON 파일로 저장
void client_member_join(ClientInfo *client, cJSON *request_root);                   // 회원가입 처리 함수
void client_login(ClientInfo *client, cJSON *request_root);                         // 로그인 함수
void change_nickname(ClientInfo *client, cJSON *request_root);                      // 닉네임 변경 처리 함수
void *handle_client(void *arg);                                                     // 클라이언트 스레드 만들어주는 함수
void message_list(int client_sock, cJSON *request_root);                            // 아이디에 담긴 메시지 배열을 받아오기
char *get_time_now(char *time_buf);                                                 // 타임 스템프 함수
void send_message(int client_sock, cJSON *request_root, char *time_buf);            // 클라이언트가 메시지를 보낼시 어떻게 저장할지  고민
void message_rewrite(int client_socket, cJSON *request_root);                       // 메세지를 읽음으로 표시해주기
void find_id(int client_sock, cJSON *request_root);                                 // 메시지 보내기 전 아이디 찾아서 확인하기
int msg_id_max(cJSON *root);                                                        //  메세지 고유 아이디를 최대값으로 설정하는 함수
void delelte_read_message(int client_socket, cJSON *request_root, int delete_mode); // 메세지를 찾아서 혹은 전체 삭제하는 함수
void create_room(ClientInfo *client, cJSON *root);                                  // 클라이언트가 방 요청시 만들 방
void send_room_list(int client_sock, cJSON *root);                                  // 클라이언트가 방 목록 요청시 최신화
void send_response(int sock, int protocol, int result);                             // 응답 함수
void broadcast_chat(ClientInfo *client, cJSON *request_root);                       // 현재 방에 있는 유저에게 메시지 보내기
void handle_exit_room(ClientInfo *client);                                          // 방장이 나갈때 로직
void auto_delete_old_messages();                                                    // 3일 지나면 자동으로 삭제해주는 로직 (문자열을 변환 오마이갓)
int is_expired(const char *recv_time_str);                                          // 문자열인 시간을 자동으로 변환 시켜서 정수로 비교하게 만들어주기
void enter_room(ClientInfo *client, cJSON *root);                                   // 방에 입장하면 실행되는함수
void save_rooms_to_file();
void notify_entry(ChatRoom *room, ClientInfo *new_client); // 다른 클라이언트가 방에 입장시 실행될 로직

int main()
{
    signal(SIGPIPE, SIG_IGN);
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_adr, clnt_adr;
    socklen_t clnt_adr_sz;
    pthread_t t_id;

    pthread_mutex_init(&mutx, NULL);
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        pthread_mutex_init(&room_list[i].room_mutex, NULL);
        room_list[i].is_active = false; // 초기 상태 설정
    }

    // 서버 소켓 생성 및 설정
    serv_sock = socket(PF_INET, SOCK_STREAM, 0); // 교수님 설정 가져옵니다
    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port = htons(5003);

    if (bind(serv_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)) == -1)
    {
        error_handling("bind() error"); // 바인딩 에러시 에러 메세지 출력
    }
    if (listen(serv_sock, 5) == -1)
    {
        error_handling("listen() error");
    }
    printf("== GOOD Talk 서버 오픈! 클라이언트 접속을 기다리는중... ==\n");

    // 메인 루프: 무한 반복하며 접속만 받음
    while (1)
    {
        clnt_adr_sz = sizeof(clnt_adr);
        // 새로운 클라이언트가 접속하면 소켓 생성
        int *arg = malloc(sizeof(int)); // 스레드에 안전하게 소켓 번호를 넘기기 위해 동적 할당
        *arg = accept(serv_sock, (struct sockaddr *)&clnt_adr, &clnt_adr_sz);

        if (*arg == -1)
        {
            free(arg);
            continue;
        }
        printf("소켓번호 %d번 클라이언트가 입장하셨습니다. \n", *arg);
        pthread_create(&t_id, NULL, handle_client, (void *)arg);
        // 스레드가 종료되면 즉시 자원을 회수
        pthread_detach(t_id);
        // 이제 메인 루프는 즉시 while의 처음으로 돌아가서 '다음' 손님을 기다림 (accept)
    }

    close(serv_sock);
    return 0;
}

void *handle_client(void *arg) // 클라이언트 스레드 만들어주는 함수
{
    // 전달받은 인자를 로컬 변수에 복사하고 메모리를 즉시 해제합니다.
    int client_sock = *((int *)arg); // 교수님꺼 응용했습니다..
    free(arg);                       // 메모리 해제
    char buffer[10000];

    ClientInfo *my_info = (ClientInfo *)malloc(sizeof(ClientInfo)); // 나 자신의 정보를 담을 구조체 생성
    if (my_info == NULL)
    {
        close(client_sock);
        return NULL;
    }
    memset(my_info, 0, sizeof(ClientInfo)); // 0으로 깨끗하게 초기화
    my_info->socket = client_sock;          // 내 소켓 번호 적어두기

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));
        int str_len = recv(client_sock, buffer, sizeof(buffer) - 1, 0);

        if (str_len <= 0)
            break;
        buffer[str_len] = '\0';            // 2. 받은 길이의 끝에 정확히 마침표(\0)를 찍습니다. (매우 중요!)
        cJSON *root = cJSON_Parse(buffer); // 이제 깨끗한 상태로 파싱합니다.

        if (root == NULL)
        {
            // 파싱 실패
            continue;
        }
        cJSON *protocol_item = cJSON_GetObjectItem(root, "protocol");

        if (protocol_item == NULL || !cJSON_IsNumber(protocol_item)) // protocol 아이템이 존재하고 숫자인지 확인
        {
            printf("오류: 프로토콜이 없는 JSON이거나 배열 형태입니다.\n");
            cJSON_Delete(root);
            return 0;
        }
        int protocol = protocol_item->valueint;
        switch (protocol)
        {
        case FIND_ID:
            find_id(client_sock, root);
            break;
        case LOGIN_START:
            client_login(my_info, root);
            break;
        case MEMBER_JOIN:
            client_member_join(my_info, root);
            break;
        case CHANGE_NICKNAME:
            change_nickname(my_info, root);
            break;
        case MSG_LIST:
            message_list(client_sock, root);
            break;
        case SEND_MSG:
            char time_str[20];
            send_message(client_sock, root, time_str);
            break;
        case READ_MSG:
            message_rewrite(client_sock, root);
            break;
        case DELETE_MSG:
            delelte_read_message(client_sock, root, 1);
            break;
        case ALL_MSG_DEL:
            delelte_read_message(client_sock, root, 2);
            break;
        case CATTING_ROOM_OPEN:
            create_room(my_info, root);
            break;
        case CHATTING_LIST:                    // 방 목록 요청 추가
            send_room_list(client_sock, root); // 아까 수정한 함수 호출
            break;
        case ROOM_JOIN_REQ:
            enter_room(my_info, root);
            break;
        case CHATTING_MSG: // 채팅 메시지 전송
            broadcast_chat(my_info, root);
            break;
        case EXIT_ROOM: // 방 나가기 요청
            handle_exit_room(my_info);
            break;
        }
        cJSON_Delete(root);
    }

    if (my_info->current_room)
    {
        handle_exit_room(my_info); // 방에 있다면 나가기 처리
    }
    close(client_sock);
    free(my_info); // 구조체 해제
    printf("클라이언트 접속을 종료 하셨습니다.\n");
    return NULL;
}

void error_handling(char *message) // 에러 처리 함수
{
    fputs(message, stderr); // 표준 에러 출력 스트림에 메시지 출력
    fputc('\n', stderr);    // 표준 에러 출력 스트림에 개행 문자 출력
    exit(1);                // 비정상 프로그램 종료
}
void save_user_data(User *user) // JSON 파일로 회원 정보 저장하기
{
    const char *filename = "GoodTalk.json"; // 굿톡 이름으로 JSON 파일을 만듭니다.
    cJSON *root = NULL;                     // JSON 루트 객체 포인터
    char *file_data = NULL;                 // 파일 데이터 저장용 포인터

    FILE *fp = fopen(filename, "r");
    if (fp)
    {
        fseek(fp, 0, SEEK_END);              // 파일 끝으로 이동
        long len = ftell(fp);                // 파일 길이 얻기
        fseek(fp, 0, SEEK_SET);              // 파일 시작으로 이동
        file_data = (char *)malloc(len + 1); // 파일 데이터 저장용 메모리 할당
        fread(file_data, 1, len, fp);        // 파일 읽기
        file_data[len] = '\0';               // 널 종료 문자 추가
        fclose(fp);

        root = cJSON_Parse(file_data);
        free(file_data);
    }

    if (root == NULL) // 파일이 없거나 비어있으면
    {
        root = cJSON_CreateArray(); // 파일이 없거나 비어있으면 새 배열 생성
    }

    cJSON *user_obj = cJSON_CreateObject();                        // 새 유저 객체 생성
    cJSON_AddStringToObject(user_obj, "user_id", user->user_id);   // 유저 정보 추가
    cJSON_AddStringToObject(user_obj, "password", user->password); //  유저 정보 추가
    cJSON_AddStringToObject(user_obj, "nickname", user->nickname); // 유저 정보 추가

    // 이 유저가 나중에 메시지를 받을 메시지 배열 공간도 미리 구조를 잡아둠
    cJSON_AddItemToObject(user_obj, "messages", cJSON_CreateArray());

    cJSON_AddItemToArray(root, user_obj); // 유저 객체를 배열에 추가

    char *re_arr = cJSON_Print(root); // JSON 객체를 문자열로 변환
    fp = fopen(filename, "w");        // 쓰기 모드로 파일 열기
    if (fp)
    {
        fputs(re_arr, fp); // 파일에 데이터 쓰기
        fclose(fp);        // 파일 닫기
    }
    free(re_arr); // 문자열 메모리 해제
}
void client_member_join(ClientInfo *client, cJSON *request_root)
{

    cJSON *data = cJSON_GetObjectItem(request_root, "data"); // 클라이언트가 보낸 루트를 확인합니다.
    if (!data)
        return; // 데이터가 정상적이지 않다면 다시 돌려보냅니다

    cJSON *id_obj = cJSON_GetObjectItem(data, "user_id");    // 아이디를 가져옵니다
    cJSON *pw_obj = cJSON_GetObjectItem(data, "password");   // 비밀번호를 가져옵니다
    cJSON *nick_obj = cJSON_GetObjectItem(data, "nickname"); // 닉네임을 가져옵니다

    // 모든 필수 데이터가 존재하는지 확인
    if (!id_obj || !pw_obj || !nick_obj)
        return;

    pthread_mutex_lock(&mutx);

    if (is_user_exists(id_obj->valuestring))
    {
        // 중복 시 실패 응답 전송
        cJSON *response = cJSON_CreateObject();
        cJSON_AddNumberToObject(response, "protocol", ACK_MEMBER_JOIN); // 응답헤더
        cJSON *res_data = cJSON_CreateObject();                         // 객체 생성
        cJSON_AddStringToObject(res_data, "result", "실패");            // 아이디가 존재하므로 실패
        cJSON_AddItemToObject(response, "data", res_data);

        char *json_str = cJSON_PrintUnformatted(response);
        if (json_str)
        {
            char packet[BUFFER_SIZE];
            sprintf(packet, "%s\n", json_str);
            send(client->socket, json_str, strlen(json_str), 0);
            free(json_str);
        }
        cJSON_Delete(response);
        pthread_mutex_unlock(&mutx);
        return; // 가입 절차 중단
    }

    User user;

    strncpy(user.user_id, id_obj->valuestring, sizeof(user.user_id) - 1); // strncpy 사용해서 오버플로우 방지하기위해 길이를 딱 맞춰서 복사해옵니다
    user.user_id[sizeof(user.user_id) - 1] = '\0';                        // 혹시 그걸 넘어가면 길이에 맞춰 널값을 넣어줍니다

    strncpy(user.password, pw_obj->valuestring, sizeof(user.password) - 1); // strncpy 사용해서 오버플로우 방지하기위해 길이를 딱 맞춰서 복사해옵니다
    user.password[sizeof(user.password) - 1] = '\0';                        // 혹시 그걸 넘어가면 길이에 맞춰 널값을 넣어줍니다

    strncpy(user.nickname, nick_obj->valuestring, sizeof(user.nickname) - 1); // strncpy 사용해서 오버플로우 방지하기위해 길이를 딱 맞춰서 복사해옵니다
    user.nickname[sizeof(user.nickname) - 1] = '\0';                          // 혹시 그걸 넘어가면 길이에 맞춰 널값을 넣어줍니다

    strncpy(client->nickname, nick_obj->valuestring, sizeof(client->nickname) - 1); // strncpy 사용해서 오버플로우 방지하기위해 길이를 딱 맞춰서 복사해옵니다
    client->nickname[sizeof(client->nickname) - 1] = '\0';                          // 혹시 그걸 넘어가면 길이에 맞춰 널값을 넣어줍니다

    save_user_data(&user); // 유저 데이터를 JSON 파일로 저장

    pthread_mutex_unlock(&mutx);

    cJSON *response = cJSON_CreateObject();                         // 응답 객체 생성
    cJSON_AddNumberToObject(response, "protocol", ACK_MEMBER_JOIN); // 903 전송

    cJSON *res_data = cJSON_CreateObject();              // 응답 데이터 객체 생성
    cJSON_AddStringToObject(res_data, "result", "성공"); // 성공 결과 추가

    cJSON_AddItemToObject(response, "data", res_data);

    char *json_str = cJSON_PrintUnformatted(response); // 응답을 문자열로 변환
    if (json_str)
    {
        char packet[BUFFER_SIZE];
        sprintf(packet, "%s\n", json_str);
        send(client->socket, packet, strlen(packet), 0);
        free(json_str);
    } // 응답 객체 메모리 해제
}
int is_user_exists(const char *user_id) // 중복 회원가입 막기
{
    // 읽기 모드로 엽니다.
    FILE *file = fopen("GoodTalk.json", "r");
    if (!file)
    {
        // 파일이 없다는 건 아직 가입자가 한 명도 없음
        return 0;
    }

    // 파일의 전체 길이를 계산하여 버퍼에 담습니다.
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *data = (char *)malloc(length + 1);
    if (!data)
    {
        fclose(file);
        return 0;
    }
    fread(data, 1, length, file);
    data[length] = '\0';
    fclose(file);

    // 2. cJSON_Parse로 파싱합니다.
    cJSON *root = cJSON_Parse(data);
    free(data); // 파싱 후 원본 문자열은 바로 해제

    if (!root)
    {
        return 0; // 파싱 실패 시
    }

    int exists = 0;
    cJSON *user_obj = NULL;

    cJSON_ArrayForEach(user_obj, root) // 배열을 돌며 id_obj->valuestring과 user_id를 비교합니다.
    {
        cJSON *id_item = cJSON_GetObjectItem(user_obj, "user_id");
        if (id_item && id_item->valuestring != NULL)
        {
            if (strcmp(id_item->valuestring, user_id) == 0)
            {
                exists = 1; // 일치하는 아이디 발견
                break;
            }
        }
    }
    cJSON_Delete(root);
    return exists;
}
void client_login(ClientInfo *client, cJSON *request_root) // 로그인 함수
{
    const char *user_id = NULL;
    const char *user_nickname = NULL;
    cJSON *data = cJSON_GetObjectItem(request_root, "data"); // 클라이언트 데이터를 받아오기
    if (!data)
        return; // 데이터가 온전하지 않으면 돌아가라

    const char *member_id = cJSON_GetObjectItem(data, "user_id")->valuestring;  // 아이디를 받아오기
    const char *member_pw = cJSON_GetObjectItem(data, "password")->valuestring; //  비밀번호도 받아오기
    cJSON *nick_obj = cJSON_GetObjectItem(data, "nickname");
    const char *member_nick = NULL;
    if (nick_obj && nick_obj->valuestring)
    {
        member_nick = nick_obj->valuestring;
    }
    else
    {
        // 닉네임이 없으면 아이디를 닉네임으로 사용하거나 빈칸 처리
        member_nick = member_id;
    }

    User user;
    strncpy(user.user_id, member_id, sizeof(user.user_id) - 1);
    user.user_id[sizeof(user.user_id) - 1] = '\0'; // 딱 크기만큼 받아오는데 널값을 넣어야하기 때문에 한칸 줄이고 설계합니다

    strncpy(client->user_id, member_id, sizeof(client->user_id) - 1);
    client->user_id[sizeof(client->user_id) - 1] = '\0';

    strncpy(user.password, member_pw, sizeof(user.password) - 1);
    user.password[sizeof(user.password) - 1] = '\0'; // 딱 크기만큼 받아오는데 널값을 넣어야하기 때문에 한칸 줄이고 설계합니다

    strncpy(user.nickname, member_nick, sizeof(user.nickname) - 1);
    user.nickname[sizeof(user.nickname) - 1] = '\0'; // 딱 크기만큼 받아오는데 널값을 넣어야하기 때문에 한칸 줄이고 설계합니다

    strncpy(client->nickname, member_nick, sizeof(client->nickname) - 1);
    client->nickname[sizeof(client->nickname) - 1] = '\0'; // 딱 크기만큼 받아오는데 널값을 넣어야하기 때문에 한칸 줄이고 설계합니다

    FILE *fp = fopen("GoodTalk.json", "r"); // JSON을 읽기 모드로 읽어 옵니다.
    if (fp == NULL)                         // 만약 파일이 안열리거나 깨져있으면 돌아갑니다
        return;

    fseek(fp, 0, SEEK_END);  // 끝으로 포인터를 옮긴후에
    long length = ftell(fp); // 그만큼의 파일 크기를 잰후
    fseek(fp, 0, SEEK_SET);  // 다시 처음으로 파일 포인터를 가지고 옵니다.

    char *contents = (char *)malloc(length + 1); // 동적 메모리에 할당합니다
    if (contents)
    {
        fread(contents, 1, length, fp); // 만약에 내용물이 담겨있으면
        contents[length] = '\0';        // 그 길이 끝에 널값을 넣어줍니다.
    }
    fclose(fp);

    cJSON *root = cJSON_Parse(contents); //
    free(contents);                      // 파싱 후 원본 문자열은 즉시 해제하여 메모리 누수를 방지합니다.

    if (root == NULL)
        return; // JSON 형식이 틀렸을 경우 대비

    cJSON *user_array = root;                        // 꺼낸 내용을 user_array 주소로 넣습니다.
    int user_count = cJSON_GetArraySize(user_array); // 그 꺼낸 배열의 크기 user_array 사이즈 만큼 변수에 담습니다.
    bool login_success = false;                      // 로그인 로직을 참 거짓으로 바꿉니다.
    char final_user_id[20] = {0};                    // 로컬 버퍼에 복사해서 안전하게 보관
    char final_nickname[25] = {0};

    for (int i = 0; i < user_count; i++)
    {
        cJSON *user_obj = cJSON_GetArrayItem(user_array, i);
        const char *json_in_id = cJSON_GetObjectItem(user_obj, "user_id")->valuestring;
        const char *json_in_pw = cJSON_GetObjectItem(user_obj, "password")->valuestring;

        if (strcmp(user.user_id, json_in_id) == 0 && strcmp(user.password, json_in_pw) == 0)
        {
            login_success = true;
            strncpy(final_user_id, json_in_id, sizeof(final_user_id) - 1);

            cJSON *nick_item = cJSON_GetObjectItem(user_obj, "nickname");
            if (nick_item && strlen(nick_item->valuestring) > 0)
            {
                strncpy(final_nickname, nick_item->valuestring, sizeof(final_nickname) - 1);
            }
            else
            {
                memset(final_nickname, 0, sizeof(final_nickname));
            }
            break;
        }
    }

    // 여기서 root를 지워도 final_user_id에 복사해뒀으니 안전함!
    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "protocol", ACK_LOGIN_START);
    cJSON *res_data = cJSON_CreateObject();

    if (login_success)
    {
        strncpy(client->user_id, final_user_id, sizeof(client->user_id) - 1);
        cJSON_AddStringToObject(res_data, "result", "성공");
        cJSON_AddStringToObject(res_data, "user_id", final_user_id);
        cJSON_AddStringToObject(res_data, "nickname", final_nickname);
    }
    else
    {
        cJSON_AddStringToObject(res_data, "result", "실패");
    }

    cJSON_AddItemToObject(response, "data", res_data);
    char *json_str = cJSON_PrintUnformatted(response);

    if (json_str)
    {
        char packet[10240];
        sprintf(packet, "%s\n", json_str); // 반드시 \n을 붙여서 패킷 경계를 표시!
        send(client->socket, packet, strlen(packet), 0);
        free(json_str);
    }
    cJSON_Delete(response);
}
void change_nickname(ClientInfo *client, cJSON *request_root) // 닉네임 변경 처리 함수
{
    cJSON *data = cJSON_GetObjectItem(request_root, "data");
    if (!data)
        return;

    cJSON *id_item = cJSON_GetObjectItem(data, "user_id");
    cJSON *nick_item = cJSON_GetObjectItem(data, "nickname");

    if (!id_item || !nick_item)
    {
        printf("오류: 필수 데이터 누락\n");
        return;
    }

    const char *member_id = client->user_id; // 인자로 받은 client에서 아이디 활용
    const char *member_nick = nick_item->valuestring;

    strncpy(client->nickname, nick_item->valuestring, sizeof(client->nickname) - 1);
    client->nickname[sizeof(client->nickname) - 1] = '\0';

    FILE *fp = fopen("GoodTalk.json", "r"); // cJSON 파일 열기
    if (fp == NULL)
        return; // 파일 열기 실패 시 종료 실행

    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    ; // 파일 크기만큼 동적 할당

    char *contents = (char *)malloc(length + 1);
    if (contents)
    {
        fread(contents, 1, length, fp); // 만약에 내용물이 담겨있으면
        contents[length] = '\0';        // 그 길이 끝에 널값을 넣어줍니다.
    }
    fclose(fp);

    cJSON *root = cJSON_Parse(contents); // 문자열을 파싱합니다
    free(contents);                      // 파싱 후 원본 문자열은 즉시 해제하여 메모리 누수를 방지합니다.

    if (root == NULL)
        return; // JSON 형식이 틀렸을 경우 대비

    cJSON *user_array = root;                        // JSON 루트가 배열 형태라고 가정하고 첫 번째 노드를 가져옴
    int user_count = cJSON_GetArraySize(user_array); // 아이디
    bool nick_change = false;

    for (int i = 0; i < user_count; i++)
    {
        cJSON *user_obj = cJSON_GetArrayItem(user_array, i);                         // JSON 배열에서 i번째 사용자 객체를 가져와서
        const char *json_id = cJSON_GetObjectItem(user_obj, "user_id")->valuestring; // json 안에 user_id 를 찾아서 비교 하기

        if (strcmp(member_id, json_id) == 0) //  만약 받은 아이디와 찾은 아이디가 맞다면
        {
            cJSON_ReplaceItemInObject(user_obj, "nickname", cJSON_CreateString(member_nick)); // 닉네임을 받아와서 그걸 클라이언트 객체와 교체한다
            nick_change = true;                                                               //
            break;
        }
    }
    if (nick_change)
    {
        FILE *fp = fopen("GoodTalk.json", "w");
        if (fp)
        {
            char *new_json = cJSON_Print(root);
            fputs(new_json, fp);
            fclose(fp);
            free(new_json);
        }
    }
    cJSON_Delete(root);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "protocol", ACK_CHANGE_NICKNAME);
    cJSON *res_data = cJSON_CreateObject();
    cJSON_AddStringToObject(res_data, "result", "성공");
    cJSON_AddStringToObject(res_data, "nickname", client->nickname);
    cJSON_AddItemToObject(response, "data", res_data);

    char *send_msg = cJSON_PrintUnformatted(response);
    if (send_msg)
    {
        char packet[BUFFER_SIZE];
        sprintf(packet, "%s\n", send_msg);
        send(client->socket, packet, strlen(packet), 0); // client->socket 활용
        free(send_msg);
    }
    cJSON_Delete(response);
}
void message_list(int client_sock, cJSON *request_root) // 아이디에 담긴 메시지 배열을 받아오기
{
    auto_delete_old_messages();                              // 3일 지난 메시지는 자동으로 삭제해주기
    cJSON *data = cJSON_GetObjectItem(request_root, "data"); // 요청 데이터 추출
    if (!data)
        return;

    const char *member_id = cJSON_GetObjectItem(data, "user_id")->valuestring; // user_id값을 추출합니다.
    FILE *fp = fopen("GoodTalk.json", "r");
    if (fp == NULL)
        return;

    fseek(fp, 0, SEEK_END);  // 파일 포인터를 끝으로 이동
    long length = ftell(fp); // 현재 포인터 위치 파일 크기 저장
    fseek(fp, 0, SEEK_SET);  // 다시 처음으로 이동

    // 파일 내용을 담을 버퍼 동적 할당
    char *contents = (char *)malloc(length + 1);
    fread(contents, 1, length, fp); // 파일 내용을 contents에 읽어옴
    contents[length] = '\0';        // 문자열 끝 지정
    fclose(fp);                     // 파일 사용이 끝났으므로 닫기

    // JSON 파싱 및 데이터 검색
    cJSON *root = cJSON_Parse(contents); // 전체 JSON 문자열을 객체 구조로 변환
    free(contents);                      // 파싱 후 원본 문자열 메모리 해제

    cJSON *user_messages_to_send = NULL;
    int user_count = cJSON_GetArraySize(root); // 저장된 전체 유저 수 확인

    // 반복문을 돌며 요청받은 user_id와 일치하는 데이터를 찾음
    for (int i = 0; i < user_count; i++)
    {
        cJSON *user_obj = cJSON_GetArrayItem(root, i);
        const char *json_id = cJSON_GetObjectItem(user_obj, "user_id")->valuestring;

        if (strcmp(member_id, json_id) == 0) // 아이디가 일치한다면
        {
            // messages 배열 포인터를 획득
            user_messages_to_send = cJSON_GetObjectItem(user_obj, "messages");
            break;
        }
    }

    cJSON *response = cJSON_CreateObject();                      // 응답 패킷 생성
    cJSON_AddNumberToObject(response, "protocol", ACK_MSG_LIST); // 응답 프로토콜

    cJSON *res_data = cJSON_CreateObject(); // 실제 데이터가 담길 객체 생성

    cJSON_AddItemToObject(res_data, "messages", cJSON_Duplicate(user_messages_to_send, 1));
    cJSON_AddItemToObject(response, "data", res_data);
    // JSON 객체를 전송 가능한 문자열로 변환
    char *json_str = cJSON_PrintUnformatted(response);
    if (json_str)
    {
        char final_packet[length + 2];
        sprintf(final_packet, "%s\n", json_str); // 끝에 \n 추가해줘야 문자열 끝을 확인
        send(client_sock, final_packet, strlen(final_packet), 0);
        free(json_str);
    }
    cJSON_Delete(response); // 생성한 응답 객체 해제
    cJSON_Delete(root);     // 파일에서 읽어온 전체 객체 해제
}
void find_id(int client_sock, cJSON *request_root)
{
    // 요청 데이터에서 검색할 ID 추출
    cJSON *data = cJSON_GetObjectItem(request_root, "data");
    if (!data)
        return;

    cJSON *id_item = cJSON_GetObjectItem(data, "user_id");
    if (!id_item)
        return;
    const char *target_id = id_item->valuestring; // 검색 타겟 아이디

    // 파일 로드
    FILE *fp = fopen("GoodTalk.json", "r"); // 파일 불러오기 로직
    if (!fp)
        return;

    fseek(fp, 0, SEEK_END); // 늘 하던거
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *contents = (char *)malloc(length + 1); // 동적 메모리 할당
    fread(contents, 1, length, fp);
    contents[length] = '\0';
    fclose(fp);

    cJSON *root = cJSON_Parse(contents); // 객체 준비 !
    free(contents);                      // 바로 해제해주기
    if (!root)
        return;

    // 아이디가 있는지 하나씩 꺼내서 비교해보기
    bool success = false;
    int user_count = cJSON_GetArraySize(root);

    for (int i = 0; i < user_count; i++)
    {
        cJSON *user_obj = cJSON_GetArrayItem(root, i);
        cJSON *json_id = cJSON_GetObjectItem(user_obj, "user_id");

        // DB에 해당 ID가 있는지 단순 비교
        if (json_id && strcmp(target_id, json_id->valuestring) == 0)
        {
            success = true;
            break;
        }
    }

    // 응답 전송 객체 생성
    cJSON *response = cJSON_CreateObject();
    cJSON *res_data = cJSON_CreateObject();
    if (success)
    {
        cJSON_AddNumberToObject(response, "protocol", SUCCESS_FINE_ID); // 성공적으로 찾았다고 반환
        cJSON_AddStringToObject(res_data, "user_id", target_id);        // 찾은 아이디 그대로 반환
    }
    else
    {
        cJSON_AddNumberToObject(response, "protocol", FAIL_FINE_ID); // 실패하면 실패했다고 반환
    }
    cJSON_AddItemToObject(response, "data", res_data);

    char *json_str = cJSON_PrintUnformatted(response);
    if (json_str)
    {
        int send_len = strlen(json_str); // 실제 보낼 데이터 길이 측정
        send(client_sock, json_str, send_len, 0);
        send(client_sock, "\n", 1, 0); // 줄바꿈 문자
        free(json_str);
    }

    cJSON_Delete(response);
    cJSON_Delete(root);
}
void send_message(int client_sock, cJSON *request_root, char *time_buf) // 클라이언트가 메시지를 보낼시 어떻게 저장할지  고민
{
    pthread_mutex_lock(&mutx);
    // 누가, 누구에게, 무엇을 보내는지 데이터 받기
    cJSON *data = cJSON_GetObjectItem(request_root, "data");
    if (!data)
    {
        pthread_mutex_unlock(&mutx); // 실패 시 락 풀고 리턴
        return;
    }
    const char *sender_id = cJSON_GetObjectItem(data, "user_id")->valuestring; // 누가 보냈는지
    const char *receiver_id = cJSON_GetObjectItem(data, "to_id")->valuestring; // 누구에게 보내는지
    cJSON *item = cJSON_GetObjectItem(data, "contents");

    char contents[512];
    memset(contents, 0, sizeof(contents)); // 일단 전체를 0으로 초기화

    // 값 가져오기
    const char *temp_ptr = cJSON_GetStringValue(item);
    if (temp_ptr != NULL)
    {
        strncpy(contents, temp_ptr, sizeof(contents) - 1); // 내 바구니로 복사 바구니 크기보다 1 적게 복사
        contents[sizeof(contents) - 1] = '\0';             // 강제로 마지막에 널 문자 넣기
    }

    FILE *fp = fopen("GoodTalk.json", "r+"); // 읽기/쓰기 모드 (전체를 다 읽고 덮어써야하니까 + 모드로)
    if (!fp)
    {
        pthread_mutex_unlock(&mutx);
        return;
    }
    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *file_contents = (char *)malloc(length + 1);
    fread(file_contents, 1, length, fp);
    file_contents[length] = '\0';

    cJSON *root = cJSON_Parse(file_contents);
    free(file_contents);

    int next_id = msg_id_max(root);

    // 수신할 고유 아이디 찾기
    cJSON *send_user = NULL;
    int user_count = cJSON_GetArraySize(root);
    for (int i = 0; i < user_count; i++)
    {
        cJSON *user = cJSON_GetArrayItem(root, i);
        if (strcmp(cJSON_GetObjectItem(user, "user_id")->valuestring, receiver_id) == 0) // 이건 우리가 보내는 사람을 제이슨 안에서 찾고 그 사람의 아이디를 비교하는 로직
        {
            send_user = user;
            break;
        }
    }

    if (send_user != NULL)
    {
        cJSON *msg_array = cJSON_GetObjectItem(send_user, "messages");
        // 요약 내용 및 내용 받기
        cJSON *new_msg = cJSON_CreateObject();

        cJSON_AddNumberToObject(new_msg, "msg_id", next_id);
        cJSON_AddStringToObject(new_msg, "from_id", sender_id);
        cJSON_AddStringToObject(new_msg, "recv_contents", contents);
        cJSON_AddStringToObject(new_msg, "recv_day_time", get_time_now(time_buf));
        cJSON_AddStringToObject(new_msg, "read_or_not", "안읽음");

        // 수신자의 메시지 배열에 추가
        cJSON_AddItemToArray(msg_array, new_msg);

        // 파일에 갱신된 JSON 쓰기
        char *updated_str = cJSON_Print(root);
        freopen("GoodTalk.json", "w", fp); // 파일을 비우고 새로 씀
        fprintf(fp, "%s", updated_str);
        free(updated_str);
    }
    cJSON_Delete(root); // JSON 객체 해제
    fclose(fp);         // 파일 닫기 (이때 저장이 완료됨)

    pthread_mutex_unlock(&mutx);
    // 결과 응답 전송
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "protocol", ACK_MSG_LIST); // 응답 프로토콜
    char *res_str = cJSON_PrintUnformatted(response);
    cJSON_AddStringToObject(response, "result", (send_user ? "성공" : "실패")); // 삼항 연산자
    if (res_str)
    {
        // 전송할 데이터의 실제 길이를 측정
        int send_len = strlen(res_str);
        // 전송 로직
        send(client_sock, res_str, send_len, 0);
        send(client_sock, "\n", 1, 0);
        free(res_str);
    }
    cJSON_Delete(response);
}
void message_rewrite(int client_socket, cJSON *request_root)
{
    pthread_mutex_lock(&mutx);

    cJSON *data = cJSON_GetObjectItem(request_root, "data");

    int target_msg_id = cJSON_GetObjectItem(data, "msg_id")->valueint;

    FILE *fp = fopen("GoodTalk.json", "r");
    if (!fp)
    {
        pthread_mutex_unlock(&mutx);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *contents = (char *)malloc(length + 1);
    fread(contents, 1, length, fp);
    contents[length] = '\0';
    fclose(fp); // 읽기 끝났으니 일단 닫습니다.

    cJSON *root = cJSON_Parse(contents);
    free(contents);

    cJSON *msg = NULL;
    cJSON *updated_msg = NULL; // 클라이언트에게 보여줄 수정된 메시지

    int user_count = cJSON_GetArraySize(root);
    int found_flag = 0; // 찾았는지 확인하는 깃발

    for (int i = 0; i < user_count; i++)
    {
        cJSON *user = cJSON_GetArrayItem(root, i);
        cJSON *msg_array = cJSON_GetObjectItem(user, "messages");
        {
            cJSON *msg_array = cJSON_GetObjectItem(user, "messages");

            // 메시지 배열 순회
            cJSON_ArrayForEach(msg, msg_array)
            {
                // 숫자로 비교합니다
                cJSON *id_obj = cJSON_GetObjectItem(msg, "msg_id");
                if (id_obj && id_obj->valueint == target_msg_id)
                {
                    cJSON_ReplaceItemInObject(msg, "read_or_not", cJSON_CreateString("읽음"));
                    found_flag = 1;

                    printf("메시지 ID %d번 '읽음' 처리 완료!\n", target_msg_id);
                    break;
                }
            }
        }
        if (found_flag)
            break;
    }
    if (found_flag)
    {
        fp = fopen("GoodTalk.json", "w"); // "w"는 기존 내용을 지우고 새로 씁니다.
        if (fp)
        {
            char *new_json_str = cJSON_Print(root);
            fprintf(fp, "%s", new_json_str);
            fclose(fp);
            free(new_json_str);
        }
    }
    else
    {
        printf("[Server] 오류: 메시지 ID %d번을 찾을 수 없습니다.\n", target_msg_id);
    }
    cJSON_Delete(root);
    pthread_mutex_unlock(&mutx);
}
char *get_time_now(char *time_buf)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    if (t == NULL)
        return "Time Error"; // 에러 시 문자열 리터럴 반환

    // strftime은 성공 시 쓴 바이트 수를 반환합니다.
    strftime(time_buf, 20, "%Y-%m-%d %H:%M:%S", t);

    return time_buf; // 인자로 받은 버퍼의 주소를 그대로 반환
}
void delelte_read_message(int client_socket, cJSON *request_root, int delete_mode) // 메시지 삭제 하기 로직
{

    pthread_mutex_lock(&mutx); // 뮤택스 설정

    cJSON *data = cJSON_GetObjectItem(request_root, "data");
    const char *send_id = cJSON_GetObjectItem(data, "user_id")->valuestring; // 요청한 사람의 아이디

    FILE *fp = fopen("GoodTalk.json", "r");
    if (!fp)
    {
        pthread_mutex_unlock(&mutx);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *contents = (char *)malloc(length + 1);
    fread(contents, 1, length, fp);
    contents[length] = '\0';
    fclose(fp);

    cJSON *root = cJSON_Parse(contents);
    free(contents);

    int user_count = cJSON_GetArraySize(root); // 배열 크기만큼 돌려돌려
    int found_flag = 0;

    for (int i = 0; i < user_count; i++)
    {
        cJSON *user = cJSON_GetArrayItem(root, i);                                   // 첫 배열 까자
        if (strcmp(cJSON_GetObjectItem(user, "user_id")->valuestring, send_id) == 0) // 요청한 아이디와 안에 아이디가 같다면
        {

            cJSON *msg_array = cJSON_GetObjectItem(user, "messages");

            if (delete_mode == 2) // NOT_READ_MSG  === 전체 메시지 삭제 ===
            {
                cJSON_ReplaceItemInObject(user, "messages", cJSON_CreateArray());
                found_flag = 1;
            }
            else if (delete_mode == 1) // DELETE_MSG  == 읽은 것만 삭제 ==
            {
                int count = cJSON_GetArraySize(msg_array);
                // 뒤에서부터 지워야 한다네요
                for (int j = count - 1; j >= 0; j--) // 뒤로돌아가서 하나씩 삭제
                {
                    cJSON *m = cJSON_GetArrayItem(msg_array, j);
                    cJSON *st = cJSON_GetObjectItem(m, "read_or_not");
                    if (st && strcmp(st->valuestring, "읽음") == 0)
                    {
                        cJSON_DeleteItemFromArray(msg_array, j);
                        found_flag = 1;
                    }
                }
            }
            break;
        }
    }
    // 변경사항 저장
    if (found_flag) // 다 통과했다면 파일 쓰고 저장하기
    {
        fp = fopen("GoodTalk.json", "w");
        char *p = cJSON_Print(root);
        fprintf(fp, "%s", p);
        free(p);
        fclose(fp);
    }
    cJSON_Delete(root);
    pthread_mutex_unlock(&mutx);

    cJSON *res = cJSON_CreateObject();
    // 요청이 910이면 응답은 911, 요청이 912면 응답은 913
    int ack_proto = (delete_mode == 1) ? ACK_DELETE_MSG : ALL_MSG_DEL;

    cJSON_AddNumberToObject(res, "protocol", ack_proto); // 그거에 맞게 저장해서 포장하기

    cJSON *res_data = cJSON_CreateObject();                                               // 그안에 데이터 넣기
    cJSON_AddStringToObject(res_data, "result", found_flag ? "성공" : "실패(또는 없음)"); // flag가 1이면 성공  0이면 실패
    cJSON_AddItemToObject(res, "data", res_data);

    char *res_str = cJSON_PrintUnformatted(res);
    char packet[256];
    sprintf(packet, "%s\n", res_str);
    send(client_socket, packet, strlen(packet), 0);

    free(res_str);
    cJSON_Delete(res);
}
int msg_id_max(cJSON *root) // 고유 메세지 번호를 만드는 함수
{
    int max_id = 0;                            // 현재까지 발견된 가장 큰 번호를 저장할 변수
    int user_count = cJSON_GetArraySize(root); // 전체 유저가 몇 명인지 확인

    for (int i = 0; i < user_count; i++)
    {
        cJSON *user_obj = cJSON_GetArrayItem(root, i);                // i번째 유저를 꺼냄
        cJSON *msg_array = cJSON_GetObjectItem(user_obj, "messages"); // 그 유저의 메시지함을 가져옴

        if (cJSON_IsArray(msg_array)) // 메시지함이 배열 형태가 맞는지 확인
        {
            cJSON *temp_msg = NULL;
            cJSON_ArrayForEach(temp_msg, msg_array) // 메시지 개수만큼 반복
            {
                cJSON *id_obj = cJSON_GetObjectItem(temp_msg, "msg_id");
                if (id_obj && id_obj->valueint > max_id)
                {
                    max_id = id_obj->valueint;
                }
            }
        }
    }
    return max_id + 1; // 겹치지 않는 다음 번호
}
void create_room(ClientInfo *client, cJSON *root)
{
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data)
        return;

    // 아이디, 제목, 비밀번호 객체를 꺼냅니다.
    cJSON *id_obj = cJSON_GetObjectItem(data, "user_id");
    cJSON *title_obj = cJSON_GetObjectItem(data, "room_title");
    cJSON *pw_obj = cJSON_GetObjectItem(data, "password");

    // 하나라도 없으면 방 생성 거부
    if (!id_obj || !title_obj || !pw_obj)
    {
        printf("[오류] 방 생성 데이터 누락\n");
        return;
    }

    ChatRoom *new_room = NULL;

    pthread_mutex_lock(&mutx);
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (!room_list[i].is_active)
        {
            new_room = &room_list[i];
            break;
        }
    }

    if (new_room)
    {
        pthread_mutex_lock(&new_room->room_mutex);
        new_room->is_active = true;
        new_room->host_sock = client->socket;

        // 문자열 복사
        strncpy(new_room->room_title, title_obj->valuestring, sizeof(new_room->room_title) - 1);
        new_room->room_title[sizeof(new_room->room_title) - 1] = '\0';

        // 비밀번호도 문자열로 복사
        strncpy(new_room->password, pw_obj->valuestring, sizeof(new_room->password) - 1);
        new_room->password[sizeof(new_room->password) - 1] = '\0';

        for (int j = 0; j < MAX_CLIENTS_PER_ROOM; j++)
        {
            new_room->client_socks[j] = -1;
        }
        new_room->client_socks[0] = client->socket;
        new_room->user_count = 1;

        client->current_room = new_room;
        pthread_mutex_unlock(&new_room->room_mutex);
    }

    pthread_mutex_unlock(&mutx); // 전체 락 해제
    save_rooms_to_file();        // 파일 저장

    send_response(client->socket, ACK_ROOM_OPEN, new_room ? ACK_ROOM_OPEN : ACK_FAIL_ROOM_OPEN);
}
void send_response(int sock, int protocol, int result) // 응답 함수
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "protocol", protocol);

    cJSON *res_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(res_data, "result", result);
    cJSON_AddItemToObject(response, "data", res_data);

    char *json_str = cJSON_PrintUnformatted(response);
    if (json_str)
    {
        char packet[BUFFER_SIZE];
        sprintf(packet, "%s\n", json_str);
        send(sock, packet, strlen(packet), 0);
        free(json_str);
    }
}
void send_room_list(int client_sock, cJSON *root)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "protocol", CHATTING_LIST); // 채팅방 목록 요청시 최신화

    cJSON *rooms_array = cJSON_CreateArray(); // 배열로 각 방에 대한 정보를 가져옵니다.

    pthread_mutex_lock(&mutx);
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (room_list[i].is_active) // 만약 방이 활성화 중이라면
        {
            cJSON *room_item = cJSON_CreateObject();
            cJSON_AddNumberToObject(room_item, "room_id", i);                          // 방번호 넘기기
            cJSON_AddStringToObject(room_item, "room_title", room_list[i].room_title); // 방제목
            cJSON_AddStringToObject(room_item, "password", room_list[i].password);     // 방 비밀번호 (저장용)
            cJSON_AddNumberToObject(room_item, "user_count", room_list[i].user_count); // 유저수
            cJSON_AddItemToArray(rooms_array, room_item);                              // 배열로 보내서 방 하나씩 출력하게 만들기
        }
    }
    pthread_mutex_unlock(&mutx);

    cJSON_AddItemToObject(response, "rooms", rooms_array);
    char *json_str = cJSON_PrintUnformatted(response);
    char final_packet[BUFFER_SIZE];
    sprintf(final_packet, "%s\n", json_str);
    send(client_sock, final_packet, strlen(final_packet), 0);

    free(json_str);
    cJSON_Delete(response);
}
void handle_exit_room(ClientInfo *client)
{
    ChatRoom *room = client->current_room;
    if (!room)
        return;

    pthread_mutex_lock(&room->room_mutex);

    bool is_host_leaving = (room->host_sock == client->socket);

    if (is_host_leaving)
    {
        // 방 폭파 로직
        cJSON *notice = cJSON_CreateObject();
        cJSON_AddNumberToObject(notice, "protocol", ACK_ROOM_CLOSE);
        cJSON_AddStringToObject(notice, "message", "방장이 퇴장하여 방이 종료되었습니다.");

        char *json_str = cJSON_PrintUnformatted(notice);
        char packet[BUFFER_SIZE];
        sprintf(packet, "%s\n", json_str);

        for (int i = 0; i < MAX_CLIENTS_PER_ROOM; i++)
        {
            if (room->client_socks[i] != -1)
            {
                send(room->client_socks[i], packet, strlen(packet), 0);
                room->client_socks[i] = -1;
            }
        }
        room->is_active = false;
        room->user_count = 0;

        free(json_str);
        cJSON_Delete(notice);
    }
    else
    { // 일반 유저 퇴장 시: 인원수 감소 및 소켓 제거
        for (int i = 0; i < MAX_CLIENTS_PER_ROOM; i++)
        {
            if (room->client_socks[i] == client->socket)
            {
                room->client_socks[i] = -1;
                if (room->user_count > 0)
                    room->user_count--;
                break;
            }
        }

        // [추가] 본인에게 나갔음을 알리는 패킷 전송 (매우 중요!)
        cJSON *res = cJSON_CreateObject();
        cJSON_AddNumberToObject(res, "protocol", ACK_EXIT_ROOM); // 또는 클라이언트가 정의한 ACK 프로토콜
        char *json_str = cJSON_PrintUnformatted(res);
        char packet[1024];
        sprintf(packet, "%s\n", json_str);
        send(client->socket, packet, strlen(packet), 0);

        free(json_str);
        cJSON_Delete(res);
    }

    pthread_mutex_unlock(&room->room_mutex);

    client->current_room = NULL; // 이 코드가 실행되어야 다음 broadcast_chat이 안 나감
    save_rooms_to_file();
}

void broadcast_chat(ClientInfo *client, cJSON *request_root)
{
    ChatRoom *room = client->current_room;
    if (!room)
        return;

    cJSON *data = cJSON_GetObjectItem(request_root, "data");
    if (!data)
        return; // 데이터 객체 확인

    cJSON *msg_obj = cJSON_GetObjectItem(data, "message");
    if (!msg_obj || !msg_obj->valuestring)
        return; // 메시지 문자열 확인

    const char *msg = msg_obj->valuestring;
    // 전송할 JSON 패킷 생성
    cJSON *chat_pkg = cJSON_CreateObject();                             // 바로 쏴주기
    cJSON_AddNumberToObject(chat_pkg, "protocol", CHATTING_MSG);        // 채팅 프로토콜
    cJSON *res_data = cJSON_CreateObject();                             // 그 안에 데이터를 넣습니다
    cJSON_AddStringToObject(res_data, "sender_id", client->user_id);    // 보내는 사람 받고
    cJSON_AddStringToObject(res_data, "sender_nick", client->nickname); // 닉네임까지 보내기
    cJSON_AddStringToObject(res_data, "message", msg);                  // 다시 보내기
    cJSON_AddItemToObject(chat_pkg, "data", res_data);                  // 파일 크기 재기

    char *json_str = cJSON_PrintUnformatted(chat_pkg); // 데이터를 주소에 저장
    char final_packet[BUFFER_SIZE];                    // 사이즈
    sprintf(final_packet, "%s\n", json_str);           // \n를 넣어서 구분하기

    pthread_mutex_lock(&room->room_mutex);

    for (int i = 0; i < MAX_CLIENTS_PER_ROOM; i++) // 채팅방에 있는 모든 소켓을 돌면서
    {
        if (room->client_socks[i] != -1)
        {
            // 방 안의 모든 소켓 전부에게 메세지 전송
            send(room->client_socks[i], final_packet, strlen(final_packet), 0);
        }
    }
    pthread_mutex_unlock(&room->room_mutex);

    free(json_str);
    cJSON_Delete(chat_pkg);
}
void save_rooms_to_file() // 방 목록을 JSON 파일로 저장하는 동기화 함수
{
    pthread_mutex_lock(&mutx);

    cJSON *root = cJSON_CreateObject();
    cJSON *rooms_array = cJSON_CreateArray();

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (room_list[i].is_active)
        {
            cJSON *room_item = cJSON_CreateObject();
            cJSON_AddNumberToObject(room_item, "room_id", i);
            cJSON_AddStringToObject(room_item, "room_title", room_list[i].room_title);
            cJSON_AddNumberToObject(room_item, "user_count", room_list[i].user_count);

            // [수정] 비밀번호를 문자열(AddString)로 저장해야 함!
            cJSON_AddStringToObject(room_item, "password", room_list[i].password);

            cJSON_AddItemToArray(rooms_array, room_item);
        }
    }
    cJSON_AddItemToObject(root, "rooms", rooms_array); // JSON을 배열 형식으로 만들고
    char *json_str = cJSON_Print(root);                // 정리해서 넣기 위해
    // 파일에 덮어쓰기
    FILE *fp = fopen("GoodChatting.json", "w");
    if (fp)
    {
        fprintf(fp, "%s", json_str);
        fclose(fp);
    }

    free(json_str);
    cJSON_Delete(root);

    pthread_mutex_unlock(&mutx);
}
int is_expired(const char *recv_time_str)
{
    struct tm recv_tm = {0};

    // [수정 포인트] JSON 데이터가 "2026-01-30 18:14:13" 형태이므로
    // 포맷을 "%d-%d-%d %d:%d:%d"로 맞추고 변수 6개를 다 받아야 합니다.
    if (sscanf(recv_time_str, "%d-%d-%d %d:%d:%d",
               &recv_tm.tm_year, &recv_tm.tm_mon, &recv_tm.tm_mday,
               &recv_tm.tm_hour, &recv_tm.tm_min, &recv_tm.tm_sec) < 6)
    {
        return 0; // 형식이 이상하면 일단 삭제 안 함 (안전장치)
    }

    // struct tm 구조체 보정
    recv_tm.tm_year -= 1900; // 년도는 1900을 뺌
    recv_tm.tm_mon -= 1;     // 월은 0부터 시작하므로 1을 뺌
    recv_tm.tm_isdst = -1;   // 썸머타임 자동 판단

    // 1. 메시지 시간을 초 단위(time_t)로 변환
    time_t msg_time = mktime(&recv_tm);
    if (msg_time == -1)
        return 0;

    // 2. 현재 시간을 초 단위로 구함
    time_t now = time(NULL);

    // 3. 차이 계산 (현재시간 - 메시지시간)
    double diff_sec = difftime(now, msg_time);

    // 3일 = 259200초 (3 * 24 * 60 * 60)
    // 차이가 259200초보다 크면 만료된 것!
    if (diff_sec >= 259200)
    {
        return 1; // 삭제 대상
    }
    return 0; // 유지
}
void auto_delete_old_messages() // 3일 지난 메시지 자동으로 삭제하기
{
    pthread_mutex_lock(&mutx); // 파일 건드리니까 락 걸기

    FILE *fp = fopen("GoodTalk.json", "r");
    if (fp == NULL)
    {
        pthread_mutex_unlock(&mutx);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *contents = (char *)malloc(length + 1);
    fread(contents, 1, length, fp);
    contents[length] = '\0';
    fclose(fp);

    cJSON *root = cJSON_Parse(contents);
    free(contents);

    if (root == NULL)
    {
        pthread_mutex_unlock(&mutx);
        return;
    }

    int total_deleted = 0; // 몇 개 지웠는지 로그용

    int user_count = cJSON_GetArraySize(root); // 모든 유저 아이디를 돌아가면서 확인합니다
    for (int i = 0; i < user_count; i++)       // 총 인원 전부
    {
        cJSON *user = cJSON_GetArrayItem(root, i);
        cJSON *messages = cJSON_GetObjectItem(user, "messages");
        if (cJSON_IsArray(messages)) // 메시지 배열이면
        {
            int msg_count = cJSON_GetArraySize(messages); // 총 배열 개수를 변수에 저장

            // 배열 삭제 뒤에서부터 앞으로
            // 앞에서부터 지우면 인덱스가 당겨져서 건너뛰는 메시지가 생김
            for (int j = msg_count - 1; j >= 0; j--)
            {
                cJSON *msg = cJSON_GetArrayItem(messages, j);
                cJSON *time_item = cJSON_GetObjectItem(msg, "recv_day_time");

                if (time_item && time_item->valuestring)
                {
                    // 아까 만든 함수 호출
                    if (is_expired(time_item->valuestring))
                    {
                        cJSON_DeleteItemFromArray(messages, j); // 배열에서 쏙 뺌
                        total_deleted++;                        // 개수증가 시키기
                    }
                }
            }
        }
    }
    if (total_deleted > 0) // 만약 1개이상이면
    {
        fp = fopen("GoodTalk.json", "w"); // 파일을 다시 씁니다
        char *new_json = cJSON_Print(root);
        fprintf(fp, "%s", new_json);
        free(new_json);
        fclose(fp);
        printf("== 총 %d개의 오래된 메시지를 정리했습니다. ==\n", total_deleted);
    }
    cJSON_Delete(root);
    pthread_mutex_unlock(&mutx);
}
void enter_room(ClientInfo *client, cJSON *root) // 방에 입장하면 실행되는함수
{
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data)
        return;

    cJSON *id_obj = cJSON_GetObjectItem(data, "room_id");
    cJSON *user_obj = cJSON_GetObjectItem(data, "user_id"); // [수정] 일단 객체만 가져옴

    if (!id_obj)
        return; // 방 번호 없으면 무시

    int room_id = id_obj->valueint;

    // 1. 방 존재 및 활성화 확인
    if (room_id < 0 || room_id >= MAX_ROOMS || !room_list[room_id].is_active)
    {
        cJSON *response = cJSON_CreateObject();
        cJSON *res_data = cJSON_CreateObject();
        cJSON_AddStringToObject(res_data, "result", "실패");
        cJSON_AddItemToObject(response, "data", res_data);

        char *json_str = cJSON_PrintUnformatted(response);
        if (json_str)
        {
            char packet[BUFFER_SIZE];
            sprintf(packet, "%s\n", json_str);
            send(client->socket, packet, strlen(packet), 0);
            free(json_str);
        }
        return;
    }

    ChatRoom *room = &room_list[room_id];

    // 방에 자리 구하는
    pthread_mutex_lock(&room->room_mutex);
    int empty_slot = -1;
    // 빈 자리 찾기
    for (int i = 0; i < MAX_CLIENTS_PER_ROOM; i++)
    {
        if (room->client_socks[i] == -1)
        {
            empty_slot = i;
            break;
        }
    }

    // 자리가 없으면  쫓아내기
    if (empty_slot == -1)
    {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddNumberToObject(response, "protocol", ACK_JOIN_ROOM);
        cJSON *res_data = cJSON_CreateObject();
        cJSON_AddStringToObject(res_data, "result", "실패");
        cJSON_AddItemToObject(response, "data", res_data);
        pthread_mutex_unlock(&room->room_mutex);
        return;
    }
    room->client_socks[empty_slot] = client->socket; // 방에 입장시에 유저수 카운트 하기
    room->user_count++;

    client->current_room = room; // 이 방에 있다고 표시

    pthread_mutex_unlock(&room->room_mutex); // 자물쇠 해제

    save_rooms_to_file();

    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "protocol", ACK_JOIN_ROOM);
    cJSON *res_data = cJSON_CreateObject();
    cJSON_AddStringToObject(res_data, "result", "성공");
    cJSON_AddItemToObject(response, "data", res_data);

    char *json_str = cJSON_PrintUnformatted(response);
    if (json_str)
    {
        char packet[BUFFER_SIZE];
        sprintf(packet, "%s\n", json_str);
        send(client->socket, packet, strlen(packet), 0);
        free(json_str);
    }
    cJSON_Delete(response);
    notify_entry(room, client);
}
void notify_entry(ChatRoom *room, ClientInfo *new_client) // 다른 클라이언트가 방에 입장시 실행될 로직
{

    cJSON *notice = cJSON_CreateObject();
    cJSON_AddNumberToObject(notice, "protocol", CHATTING_MSG);

    cJSON *data = cJSON_CreateObject();
    // 닉네임과 함께 메시지 구성
    char msg_content[100];
    snprintf(msg_content, sizeof(msg_content), "%s님이 입장하셨습니다.", new_client->nickname);

    cJSON_AddStringToObject(data, "message", msg_content);
    cJSON_AddStringToObject(data, "sender_id", new_client->user_id);
    cJSON_AddStringToObject(data, "sender_nick", new_client->nickname); // 발신자를 시스템으로 표시
    cJSON_AddItemToObject(notice, "data", data);

    char *json_str = cJSON_PrintUnformatted(notice);
    char packet[BUFFER_SIZE];
    sprintf(packet, "%s\n", json_str);

    pthread_mutex_lock(&room->room_mutex);
    for (int i = 0; i < MAX_CLIENTS_PER_ROOM; i++)
    {
        if (room->client_socks[i] != -1)
        {
            send(room->client_socks[i], packet, strlen(packet), 0);
        }
    }
    pthread_mutex_unlock(&room->room_mutex);

    free(json_str);
    cJSON_Delete(notice);
}