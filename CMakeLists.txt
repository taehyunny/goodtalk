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
