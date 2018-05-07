//
// Created by martin on 30/04/18.
//
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include "../common/constants.h"
#include "../common/message.h"
extern "C" {
#include "../common/log/log.h"
#include "../common/ipc/msg_queue.h"
#include "../common/ipc/socket.h"
#include "../common/ipc/shm.h"
#include "../common/ipc/semaphore.h"
#include "../common/ipc/sig.h"
}

//std::string procn = "server";
bool sig_quit = false;

void requestHandler(int cfd, int q_req, int q_rep);
void replyHandler(int cfd, int q_rep);
void SIGINT_handler(int signum);

int main(int argc, char* argv[]) {
    log_info("server: COMIENZO");
    register_handler(SIGINT_handler);

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

    // Inicio conexión esperando clientes
    int sfd = create_server_socket(PUERTO_SERVER);

    // Lanzo workers
    for (int i = 0; i < CANT_SERVER_WORKERS; ++i) {
        if (fork() == 0) {
            execl("./server-worker", "./server-worker", (char *) NULL);
            exit(0);
        }
    }

    int cfd;
    while (!sig_quit) {
        // Espero clientes que se conecten; obtengo el socket apropiado
        cfd = accept_client(sfd);
        if (cfd >= 0) {
            // Corro procesos handlers para el nuevo cliente conectado
            log_info("server: Nuevo cliente, lanzo handlers");
            if (fork() == 0) {
                requestHandler(cfd, q_req, q_rep);
                close(cfd);
                exit(0);
            }
        }
    }

    // Limpieza
    close(sfd);
    qdel(q_req);
    qdel(q_rep);
    // Espero a que terminen todos los procesos hijos
    while ( wait(NULL) > 0 );
    unmapshm(next_id_p);
    delshm(next_id_shm);
    delsem(next_id_sem);
    log_info("server: TERMINO");
    return 0;
}

void requestHandler(int cfd, int q_req, int q_rep) {
//    procn = "server-requestHandler";
    int p_rh = fork();
    if (p_rh == 0) {
        replyHandler(cfd, q_rep);
        exit(0);
    }
    ssize_t r;
    struct msg_t m;

    while (!sig_quit) {
        // Recibo mensaje del cliente por red
        log_debug("server-requestHandler: Espero por red próximo mensaje del cliente");//
        r = recv(cfd, &m, sizeof(m), 0);
        if (r <= 0) {
            if (r == 0 || sig_quit) break;
            log_error("server-requestHandler: Error al recibir mensaje del cliente por red. Sigo");
        } else {
            log_debug("server-requestHandler: Recibí mensaje por red:");//
            m.show();//
            m.mtype = cfd;
            // Reenvío mensaje a algún worker por cola interna
            qsend(q_req, &m, sizeof(m));
        }
    }
    kill(p_rh, SIGINT);
    log_debug("server-requestHandler: Termino");//
}

void replyHandler(int cfd, int q_rep) {
    struct msg_t m;

    while (!sig_quit) {
        log_debug("server-replyHandler: Espero próximo mensaje en q_rep");//
        if (qrecv(q_rep, &m, sizeof(m), cfd) < 0) {
            if (sig_quit) break;
            log_warn("server-replyHandler: Error al recibir un mensaje de q_rep. Sigo intentando");
        } else {
            log_debug("server-replyHandler: Recibí por cola reply de un worker:");//
            m.show();//
            m.mtype = 1; // Oculto server-side info
            // Reenvío mensaje al cliente por red
            if (send(cfd, &m, sizeof(m), 0) < 0) {
                log_error("server-replyHandler: Error al enviar mensaje al cliente");
            }
        }
    }
    log_debug("server-replyHandler: Termino");//
}

void SIGINT_handler(int signum) {
    if (signum != SIGINT) {
        log_error("server: Atrapé señal distinta de SIGINT: " + signum);
    } else {
        log_info("server: SIGINT");
        sig_quit = true;
    }
}
