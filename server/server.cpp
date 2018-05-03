//
// Created by martin on 30/04/18.
//

#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include "../common/constants.h"
#include "../common/message.h"
extern "C" {
#include "../common/log/log.h"
#include "../common/ipc/msg_queue.h"
#include "../common/ipc/socket.h"
#include "../common/ipc/shm.h"
#include "../common/ipc/semaphore.h"
}

void requestHandler(int cfd, int q_req, int q_rep);
void replyHandler(int cfd, int q_rep);

int main(int argc, char* argv[]) {
    // Creo colas internas
    int q_req = qcreate(SERVER_REQ_Q_ID);
    int q_rep = qcreate(SERVER_REP_Q_ID);
    if (q_req < 0 || q_rep < 0) {
        log_error("server: Error al crear msg queue. Freno");
        exit(-1);
    }

    // Creo shm para próximo id y sem para acceder
    int next_id_sem = creasem(SERVER_NEXT_ID_SEM_ID);
    inisem(next_id_sem, 1);
    int next_id_shm = creashm(SERVER_NEXT_ID_SHM_ID, sizeof(int));
    int* next_id_p = (int*) mapshm(next_id_shm);
    *next_id_p = SERVER_FIRST_ID;

    // Lanzo workers    ///TODO
    /*for (int i = 0; i < CANT_SERVER_WORKERS; ++i) {
        if (fork() == 0) {
            exec l o v(...);
            exit(0);
        }
    }*/

    // Inicio conexión esperando clientes
    int sfd = create_server_socket(PUERTO_SERVER);

    while (true) {
        // Espero clientes que se conecten; obtengo el socket apropiado
        int cfd = accept_client(sfd);

        // Corro procesos handlers para el nuevo cliente conectado
        if (fork() == 0) {
            requestHandler(cfd, q_req, q_rep);
            close(cfd);
        }
    }
    ///TODO: Handler que cierre el socket
//    close(sfd);
//    unmapshm(next_id_p);
//    delshm(next_id_shm);
//    delsem(next_id_sem);
//    qdel(q_req);
//    qdel(q_rep);
//    return 0;
    ///TODO: Ppalmente acá, handlear bien que hayan cerrado los sockets
}

void requestHandler(int cfd, int q_req, int q_rep) {
    if (fork() == 0) {
        replyHandler(cfd, q_rep);
        return;
    }
    struct msg_t m;

    while (true) {
        // Recibo mensaje del cliente por red
        log_debug("server-requestHandler: Espero próximo mensaje por red del cliente");//
        if (recv(cfd, &m, sizeof(m), 0) < 0) {
            log_error("server-requestHandler: Error al recibir mensaje del cliente por red. Sigo");
        } else {
            log_debug("server-requestHandler: Recibí mensaje por red:");//
            m.show();//
            ///Cambiar algo de m? mtype o id? Y si es CREATE_MSG?
            m.mtype = cfd;  ///TODO: Chequear esto de cfd como mtype
            // Reenvío mensaje a algún worker por cola interna
            qsend(q_req, &m, sizeof(m));
        }
    }
}

void replyHandler(int cfd, int q_rep) {
    struct msg_t m;

    while (true) {
        log_debug("server-requestHandler: Espero próximo mensaje en q_rep");//
        if (qrecv(q_rep, &m, sizeof(m), cfd) < 0) { ///TODO: Chequear esto de cfd como mtype
            log_warn("server-requestHandler: Error al recibir un mensaje de q_rep. Sigo intentando");
        } else {
            log_debug("server-requestHandler: Recibí por cola reply de un worker:");//
            m.show();//
            ///Cambiar algo de m? mtype o id? Y si es CREATE_MSG?
            m.mtype = m.id;  ///Algún propósito?
            // Reenvío mensaje al cliente por red
            if (send(cfd, &m, sizeof(m), 0) < 0) {
                log_error("server-requestHandler: Error al enviar mensaje al cliente");
            }
        }
    }
}