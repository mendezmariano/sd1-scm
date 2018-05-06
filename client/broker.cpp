//
// Created by martin on 28/04/18.
//
#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <sys/wait.h>
#include "../common/constants.h"
#include "../common/message.h"
extern "C" {
#include "../common/ipc/semaphore.h"
#include "../common/ipc/shm.h"
#include "../common/ipc/msg_queue.h"
#include "../common/ipc/socket.h"
#include "../common/ipc/sig.h"
#include "../common/log/log.h"
}

bool sig_quit = false;

int devolverMensajeRecibido(int q_storedmsg, struct msg_t* m, int user_id);
void requester(int* ids_p, int q_req, int q_rep, int q_storedmsg, int sfd);
void replier(int *ids_p, int q_rep, int q_storedmsg, int sfd);
void SIGINT_handler(int signum);

int main(int argc, char* argv[]) {
    register_handler(SIGINT_handler);

    // Conecto con el servidor, obtengo el socket file descriptor
    int sfd = create_client_socket(IP_SERVER, PUERTO_SERVER);
    if (sfd < 0) {
        log_error("broker: Error al crear socket cliente. Freno");
        exit(-1);
    }
    // Creo las colas
    int q_req = qcreate(LOCAL_REQ_Q_ID);
    int q_rep = qcreate(LOCAL_REP_Q_ID);
    int q_storedmsg = qcreate(STORED_MESSAGES_ID);
    if (q_req < 0 || q_rep < 0 || q_storedmsg < 0) {
        log_error("broker: Error al crear msg queue. Freno");
        exit(-1);
    }
    // Creo shm para ids globales y próximo id, y sem para acceder
    int ids_sem = creasem(LOCAL_IDS_SEM_ID);
    inisem(ids_sem, 1);
    int ids_shm = creashm(LOCAL_IDS_SHM_ID, LOCAL_MAX_ID * sizeof(int));
    int* ids_p = (int*) mapshm(ids_shm);
    ids_p[0] = LOCAL_FIRST_ID;
    for (int i = 1; i < LOCAL_MAX_ID; ++i) {
        ids_p[i] = 0;
    }

    // Corro procesos
    pid_t p_req;
    p_req = fork();
    if (p_req == 0) {
        requester(ids_p, q_req, q_rep, q_storedmsg, sfd);
        unmapshm(ids_p);
    } else {
        pid_t p_rep = fork();
        if (p_rep == 0) {
            replier(ids_p, q_rep, q_storedmsg, sfd);
            unmapshm(ids_p);

        // Espero a que terminen y cierro los recursos
        } else {
            while (!sig_quit);    ///Legal?
            kill(p_req, SIGINT);    ///Inseguro si hace falta. Chequeable
            kill(p_rep, SIGINT);
            close(sfd);
            qdel(q_req);
            qdel(q_rep);
            qdel(q_storedmsg);
            unmapshm(ids_p);
            // Espero a que terminen todos los procesos hijos
            while (wait(NULL) > 0);
            delshm(ids_shm);
            delsem(ids_sem);
        }
    }
    return 0;
}

void requester(int* ids_p, int q_req, int q_rep, int q_storedmsg, int sfd) { // No sé si me encanta esta solución
    int ids_sem = getsem(LOCAL_IDS_SEM_ID);
    struct msg_t m;

    while (!sig_quit) {
        log_debug("broker-requester: Espero próximo mensaje en q_req");//
        if (qrecv(q_req, &m, sizeof(m), 0) < 0) {
            if (sig_quit) break;
            log_warn("broker-requester: Error al recibir un mensaje de q_req. Sigo intentando");
        } else if (m.type == RECV_MSG) {
            if (devolverMensajeRecibido(q_storedmsg, &m, m.id)) { ///O lo hago de otra manera? Señal al replier?
                m.mtype = abs(m.id);
                qsend(q_rep, &m, sizeof(m));
            }
        } else {
//            // Cambio id local por global; si es nuevo lo creo, y a falta de él por 0
//            if (m.type == CREATE_MSG) {
//                p(ids_sem); {
//                    m.id = ids_p[0]++;
//                } v(ids_sem);
//                if (m.id >= LOCAL_MAX_ID) {
//                    log_warn("broker-requester: Superé cantidad máxima de ids");
//                    m.id = -1;
//                    strcpy(m.msg, "Cantidad de usuarios locales superada");
//                    qsend(q_rep, &m, sizeof(m));
//                    continue;
//                }
//            } else {
            // Cambio id local por global; a falta de él, 0
            if (m.type != CREATE_MSG) {
                p(ids_sem); {
                    m.id = ids_p[m.id];
                } v(ids_sem);
            }
            // Envío mensaje al servidor por red
            if (send(sfd, &m, sizeof(m), 0) < 0) {
                log_error("broker-requester: Error al enviar mensaje al servidor");
            }
        }
    }
}

void replier(int *ids_p, int q_rep, int q_storedmsg, int sfd) {
    int ids_sem = getsem(LOCAL_IDS_SEM_ID);
    struct msg_t m;

    while (!sig_quit) {
        // Recibo mensaje del servidor por red
        log_debug("broker-replier: Espero próximo mensaje por red del servidor");//
        if (recv(sfd, &m, sizeof(m), 0) < 0) {
            if (sig_quit) break;
            log_error("broker-replier: Error al recibir mensaje del servidor");
        } else {
            m.show();//

            // Recambio id global por id local
            // También lo meto en mtype para que cada usuario reciba solo sus mensajes
            if (m.type != CREATE_MSG) {
                p(ids_sem); {
                    int signo = (m.id < 0) ? -1 : 1;    // Mantengo señal de error
                    int i, ultimo_id = ids_p[0] - LOCAL_FIRST_ID;
                    for (i = LOCAL_FIRST_ID; i <= ultimo_id; ++i) {
                        if (abs(m.id) == ids_p[i]) {
                            m.mtype = i;
                            m.id = signo * i;
                            break;
                        }
                    }
                    if (i > ultimo_id) {
                        log_error("broker-replier: Id local no encontrado a partir del global. Ignoro reply para anon");
                        continue;
                    }
                } v(ids_sem);
            }

            switch (m.type) {
                case CREATE_MSG:    // Guarda id global para mappeo y devuelve id local al usuario
                    m.mtype = abs(m.id);
                    if (m.id >= 0) {    // Solo si fue exitoso
                        p(ids_sem); {
                            ids_p[m.id] = atoi(m.msg);
                        }
                        v(ids_sem);
                        strcpy(m.msg, std::to_string(m.id).c_str());
                    }
                    qsend(q_rep, &m, sizeof(m));
                case SUB_MSG:
                case PUB_MSG:       // Reenvían retorno al usuario
                    qsend(q_rep, &m, sizeof(m));
                    break;
                case RECV_MSG:      // Almacena mensaje
                    qsend(q_storedmsg, &m, sizeof(m));
                    break;
                case DESTROY_MSG:   // Borra del mappeo y reenvía retorno al usuario
                    p(ids_sem); {
                        ids_p[m.id] = -1;
                    } v(ids_sem);
                    qsend(q_rep, &m, sizeof(m));
                    break;
                default:
                    log_error("broker-replier: Tipo de mensaje no esperado. Freno");
                    exit(-1);
            }
        }
    }
}


int devolverMensajeRecibido(int q_storedmsg, struct msg_t* m, int user_id) {
    if (qrecv_nowait(q_storedmsg, m, sizeof(*m), user_id) >= 0) {
        log_debug("broker-requester: Tengo mensaje recibido para devolverle al cliente:");//
        m->show();//
        return 0;
    } else if (errno == ENOMSG) {
        // No hay nuevos mensajes
        log_debug("broker-requester: No hay mensajes recibidos para devolverle al cliente:");//
        m->id = -1 * m->id;
        strcpy(m->topic, "");
        strcpy(m->msg, "No hay mensajes nuevos");
        return 0;
    } else {
        log_warn("broker-requester: Error al pedir un mensaje de q_stored_msg. Sigo");
        return -1;
    }
}


void SIGINT_handler(int signum) {
    if (signum != SIGINT) {
        log_error("broker: Atrapé señal distinta de SIGINT: " + signum);
    } else {
        log_info("broker: SIGINT");
        sig_quit = true;
    }
}
