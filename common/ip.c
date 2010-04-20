/* -*- mode: C; c-basic-offset: 4;  -*- */

/*
 * Copyright (C) 2006-2010 by Hannu Jokinen
 * Full copyright text is included in the software package.
 */ 

#if defined(__WIN32__) || defined(WIN32)

#define  __USE_W32_SOCKETS
//#define Win32_Winsock

#include <windows.h>
#include <stdio.h>
#include <initguid.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#else /* UNIX */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <fcntl.h>

#endif /* WIN32 */

#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext(String)
#ifdef gettext_noop
#define N_(String) gettext_noop(String)
#else
#define N_(String) (String)
#endif
#else /* NLS is disabled */
#define _(String) (String)
#define N_(String) (String)
#define textdomain(String) (String)
#define gettext(String) (String)
#define dgettext(Domain,String) (String)
#define dcgettext(Domain,String,Type) (String)
#define bindtextdomain(Domain,Directory) (Domain) 
#define bind_textdomain_codeset(Domain,Codeset) (Codeset) 
#endif /* ENABLE_NLS */

/* System-dependent definitions */
#ifndef WIN32
#define closesocket     close
#define SOCKET          gint
#define INVALID_SOCKET  -1
#define SOCKET_ERROR    -1
#define SOCKADDR_BTH    struct sockaddr_rc
#define AF_BTH          PF_BLUETOOTH
#define BTHPROTO_RFCOMM BTPROTO_RFCOMM
#endif

#include <gtk/gtk.h>

#include "comm.h"

extern gint my_address;
extern gboolean this_is_shiai(void);
extern void copy_packet(struct message *msg);

gint pwcrc32(const guchar *str, gint len);

gulong my_ip_address = 0, node_ip_addr = 0;
gchar  my_hostname[100];
gboolean connection_ok = FALSE;
GStaticMutex send_mutex = G_STATIC_MUTEX_INIT;
static GStaticMutex rec_mutex = G_STATIC_MUTEX_INIT;

static struct {
    gint rx;
    gint tx;
    guint start;
    guint stop;
    gint rxtype[NUM_MESSAGES];
    gint txtype[NUM_MESSAGES];
} msg_stat;

gulong host2net(gulong a)
{
    return htonl(a);
}

static gint unblock_socket(gint sd) {
#ifdef WIN32
    u_long one = 1;
    if(sd != 501) // Hack related to WinIP Raw Socket support
        ioctlsocket (sd, FIONBIO, &one);
#else
    int options;
    /* Unblock our socket to prevent recvfrom from blocking forever
       on certain target ports. */
    options = O_NONBLOCK | fcntl(sd, F_GETFL);
    fcntl(sd, F_SETFL, options);
#endif //WIN32
    return 1;
}

#ifdef WIN32
gint nodescan(guint network) // addr in network byte order
{
    SOCKET sd;
    guint last, addr;
    struct sockaddr_in dst;
    gint r;
    fd_set xxx;
    gint ret;
    struct timeval timeout;

    addr = ntohl(network) & 0xffffff00;

    for (last = 1; last <= 254 && node_ip_addr == 0; last++) {
        sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sd < 0)
            return -1;
        
        unblock_socket(sd);

        struct linger l;
        l.l_onoff = 1;
        l.l_linger = 0;
        setsockopt(sd, SOL_SOCKET, SO_LINGER, (char *)&l, sizeof(struct linger));

        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_port = htons(SHIAI_PORT);
        dst.sin_addr.s_addr = htonl(addr + last);

        r = connect(sd, (const struct sockaddr *)&dst, sizeof(dst));

        g_usleep(500000);

        FD_ZERO(&xxx);
        FD_SET(sd, &xxx);
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        ret = select(0, NULL, &xxx, NULL, &timeout);
        closesocket(sd);

        if (ret > 0) {
            dst.sin_addr.s_addr = htonl(addr + last);
            g_print("%s: node found\n", inet_ntoa(dst.sin_addr));
            return dst.sin_addr.s_addr;
        }

        g_usleep(1000000);
    }

    return 0;
}

#else // !WIN32

gint nodescan(guint network) // addr in network byte order
{
    guint i, sd[16], block;
    guint last, addr;
    struct sockaddr_in dst;
    guint found = 0;

    addr = ntohl(network) & 0xffffff00;

    for (block = 0; block < 16 && node_ip_addr == 0; block++) {
        for (i = 0; i < 16; i++) {
            last = (block << 4) | i;
            if (last == 0 || last > 254)
                continue;

            memset(&dst, 0, sizeof(dst));
            dst.sin_family = AF_INET;
            dst.sin_port = htons(SHIAI_PORT);
            dst.sin_addr.s_addr = htonl(addr + last);

            sd[i] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sd < 0)
                return -1;

            unblock_socket(sd[i]);

            connect(sd[i], (const struct sockaddr *)&dst, sizeof(dst));
        }

        g_usleep(1000000);

        for (i = 0; i < 16; i++) { 
            last = (block << 4) | i;
            if (last == 0 || last > 254)
                continue;

            memset(&dst, 0, sizeof(dst));
            dst.sin_family = AF_INET;
            dst.sin_port = htons(SHIAI_PORT);
            dst.sin_addr.s_addr = htonl(addr + last);

            int ret = connect(sd[i], (const struct sockaddr *)&dst, sizeof(dst));

            closesocket(sd[i]);

            if (ret >= 0 && found == 0)
                found = dst.sin_addr.s_addr;
        }

        if (found) {
            dst.sin_addr.s_addr = found;
            g_print("%s: node found\n", inet_ntoa(dst.sin_addr));
            return found;
        }
    }

    return 0;
}
#endif //WIN32


static gulong addrs[16];
static gint   addrcnt = 0;

static gulong test_address(gulong a, gulong mask, gboolean is_not)
{
    gint i;
    for (i = 0; i < addrcnt; i++) {
        if (is_not == FALSE) {
            if ((addrs[i] & mask) == (a & mask))
                return htonl(addrs[i]);
        } else {
            if ((addrs[i] & mask) != (a & mask))
                return htonl(addrs[i]);
        }
    }
    return 0;
}

gulong get_my_address()
{
    gulong found;

    addrcnt = 0;

#ifdef WIN32
    gint i;

    SOCKET sd = WSASocket(AF_INET, SOCK_DGRAM, 0, 0, 0, 0);
    if (sd == SOCKET_ERROR) {
        return 0;
    }

    INTERFACE_INFO InterfaceList[20];
    unsigned long nBytesReturned;
    if (WSAIoctl(sd, SIO_GET_INTERFACE_LIST, 0, 0, &InterfaceList,
                 sizeof(InterfaceList), &nBytesReturned, 0, 0) == SOCKET_ERROR) {
        closesocket(sd);
        return 0;
    }

    gint nNumInterfaces = nBytesReturned / sizeof(INTERFACE_INFO);

    for (i = 0; i < nNumInterfaces; ++i) {
        struct sockaddr_in *pAddress;
        pAddress = (struct sockaddr_in *) & (InterfaceList[i].iiAddress);
        g_print("Interface %s\n", inet_ntoa(pAddress->sin_addr));

        //u_long nFlags = InterfaceList[i].iiFlags;
        if (/*(nFlags & IFF_UP) &&*/ (addrcnt < 16))
            addrs[addrcnt++] = ntohl(pAddress->sin_addr.s_addr);
    }

    closesocket(sd);

#else // !WIN32

    struct if_nameindex *ifnames, *ifnm;
    struct ifreq ifr;
    struct sockaddr_in sin;
    gint   fd;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return 0;

    ifnames = if_nameindex();
    for (ifnm = ifnames; ifnm && ifnm->if_name && ifnm->if_name[0]; ifnm++) {
        strncpy (ifr.ifr_name, ifnm->if_name, IFNAMSIZ);
        if (ioctl (fd, SIOCGIFADDR, &ifr) == 0) {
            memcpy (&sin, &(ifr.ifr_addr), sizeof (sin));
            if (addrcnt < 16)
                addrs[addrcnt++] = ntohl(sin.sin_addr.s_addr);
            g_print ("Interface %s = IP %s\n", ifnm->if_name, inet_ntoa (sin.sin_addr));
        }
    }
    if_freenameindex (ifnames);
    close(fd);

#endif // WIN32

    if ((found = test_address(0xc0000000, 0xff000000, FALSE)) != 0) // 192.x.x.x
        return found;
    if ((found = test_address(0xac000000, 0xff000000, FALSE)) != 0) // 172.x.x.x
        return found;
    if ((found = test_address(0x0a000000, 0xff000000, FALSE)) != 0) // 10.x.x.x
        return found;
    if ((found = test_address(0x7f000000, 0xff000000, TRUE)) != 0) // 127.x.x.x
        return found;
    if ((found = test_address(0x00000000, 0x00000000, FALSE)) != 0) // any address
        return found;
    return 0;
}

extern GKeyFile *keyfile;

static void node_ip_address_callback(GtkWidget *widget, 
                                     GdkEvent *event,
                                     GtkWidget *data)
{
    gulong a,b,c,d;

    if ((gulong)event == GTK_RESPONSE_OK) {
        sscanf(gtk_entry_get_text(GTK_ENTRY(data)), "%ld.%ld.%ld.%ld", &a, &b, &c, &d);
        node_ip_addr = htonl((a << 24) | (b << 16) | (c << 8) | d);
        g_key_file_set_string(keyfile, "preferences", "nodeipaddress", 
                              gtk_entry_get_text(GTK_ENTRY(data)));
    }
    gtk_widget_destroy(widget);
}

void ask_node_ip_address( GtkWidget *w,
                          gpointer   data )
{
    gchar addrstr[20];
    gulong myaddr = ntohl(node_ip_addr);
    GtkWidget *dialog, *hbox, *label, *address;

    sprintf(addrstr, "%ld.%ld.%ld.%ld", 
            (myaddr>>24)&0xff, (myaddr>>16)&0xff, 
            (myaddr>>8)&0xff, (myaddr)&0xff);

    dialog = gtk_dialog_new_with_buttons (_("Address of the communication node"),
                                          NULL,
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_STOCK_OK, GTK_RESPONSE_OK,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          NULL);

    label = gtk_label_new(_("IP address:"));
    address = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(address), addrstr);
    hbox = gtk_hbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 10);
    gtk_box_pack_start_defaults(GTK_BOX(hbox), label);
    gtk_box_pack_start_defaults(GTK_BOX(hbox), address);

    if (connection_ok)
        label = gtk_label_new(_("(Connection OK)"));
    else
        label = gtk_label_new(_("(Connection broken)"));
    gtk_box_pack_start_defaults(GTK_BOX(hbox), label);

    gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox);
    gtk_widget_show_all(dialog);

    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(node_ip_address_callback), address);
}

void show_my_ip_addresses( GtkWidget *w,
                           gpointer   data )
{
    gchar addrstr[40];
    gulong myaddr;
    GtkWidget *dialog, *vbox, *label;
    gint i;

    my_ip_address = get_my_address();

    dialog = gtk_dialog_new_with_buttons (_("Own IP addresses"),
                                          NULL,
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_STOCK_OK, GTK_RESPONSE_OK,
                                          NULL);

    vbox = gtk_vbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    for (i = 0; i < addrcnt; i++) {
        myaddr = addrs[i];
        if (myaddr == ntohl(my_ip_address))
            sprintf(addrstr, "<b>%ld.%ld.%ld.%ld</b>", 
                    (myaddr>>24)&0xff, (myaddr>>16)&0xff, 
                    (myaddr>>8)&0xff, (myaddr)&0xff);
        else
            sprintf(addrstr, "%ld.%ld.%ld.%ld", 
                    (myaddr>>24)&0xff, (myaddr>>16)&0xff, 
                    (myaddr>>8)&0xff, (myaddr)&0xff);

        label = gtk_label_new(addrstr);
        g_object_set(label, "use-markup", TRUE, NULL);
        gtk_box_pack_start_defaults(GTK_BOX(vbox), label);
    }

    gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(dialog)->vbox), vbox);

    gtk_widget_show_all(dialog);

    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(gtk_widget_destroy), NULL);
}

extern void msg_received(struct message *input_msg);
extern gint timeout_callback(gpointer data);

volatile gint msg_queue_put = 0, msg_queue_get = 0;
struct message msg_to_send[MSG_QUEUE_LEN];

static volatile gint rec_msg_queue_put = 0, rec_msg_queue_get = 0;
static struct message rec_msgs[MSG_QUEUE_LEN];

void msg_to_queue(struct message *msg)
{
    g_static_mutex_lock (&send_mutex);
    msg_to_send[msg_queue_put] = *msg;
    msg_queue_put++;
    if (msg_queue_put >= MSG_QUEUE_LEN)
        msg_queue_put = 0;
    g_static_mutex_unlock (&send_mutex);

    if (msg->type > 0 && msg->type < NUM_MESSAGES)
        msg_stat.txtype[(gint)msg->type]++;
    msg_stat.tx++;
}

struct message *get_rec_msg(void)
{
    struct message *msg;

    g_static_mutex_lock(&rec_mutex);
    if (rec_msg_queue_put == rec_msg_queue_get) {
        g_static_mutex_unlock(&rec_mutex);
        return NULL;
    }

    msg = &rec_msgs[rec_msg_queue_get];
    rec_msg_queue_get++;
    if (rec_msg_queue_get >= MSG_QUEUE_LEN)
        rec_msg_queue_get = 0;

    g_static_mutex_unlock(&rec_mutex);

    return msg;
}

gboolean check_for_input(gpointer data)
{
    struct message *msg = get_rec_msg();
        
    if (msg) {
        if (msg->type > 0 && msg->type < NUM_MESSAGES)
            msg_stat.rxtype[(gint)msg->type]++;
        msg_stat.rx++;

        msg_received(msg);
        msg->type = 0;
    }

    return TRUE;
}

static gint conn_info_callback(gpointer data)
{
    static gboolean last_ok = FALSE;
    gulong          addr;
        
    if (connection_ok == last_ok)
        return TRUE;

    last_ok = connection_ok;
    addr = ntohl(node_ip_addr);

    return TRUE;
}

void open_comm_socket(void)
{
#ifdef WIN32
    WSADATA WSAData = {0};

    if (0 != WSAStartup(MAKEWORD(2, 2), &WSAData)) {
        printf("-FATAL- | Unable to initialize Winsock version 2.2\n");
        return;
    }
#endif
    my_ip_address = get_my_address();
    g_print("Koneen osoite = %ld.%ld.%ld.%ld\n", 
            (my_ip_address)&0xff,
            (my_ip_address>>8)&0xff,
            (my_ip_address>>16)&0xff,
            (my_ip_address>>24)&0xff);

    g_timeout_add(4000, timeout_callback, NULL);
    g_timeout_add(500, conn_info_callback, NULL);
    g_timeout_add(20, check_for_input, NULL);
//        g_idle_add(check_for_input, NULL);

    msg_stat.start = time(NULL);
}

struct message *put_to_rec_queue(volatile struct message *m)
{
    struct message *ret;

    if (msg_accepted((struct message *)m) == FALSE)
        return NULL;

    g_static_mutex_lock(&rec_mutex);
    rec_msgs[rec_msg_queue_put] = *m;
    ret = &rec_msgs[rec_msg_queue_put];
    rec_msg_queue_put++;
    if (rec_msg_queue_put >= MSG_QUEUE_LEN)
        rec_msg_queue_put = 0;
    g_static_mutex_unlock(&rec_mutex);
    return ret;
}

gint send_msg(gint fd, struct message *msg)
{
    gint i = 0, err = 0, k = 0, len;
    guchar out[1024], buf[512];

    len = encode_msg(msg, buf, sizeof(buf));
    if (len < 0)
	return -1;

    out[k++] = COMM_ESCAPE;
    out[k++] = COMM_BEGIN;

    for (i = 0; i < len; i++) {
        guchar c = buf[i];

        if (k > sizeof(out) - 4)
            return -1;

        if (c == COMM_ESCAPE) {
            out[k++] = COMM_ESCAPE;
            out[k++] = COMM_FF;
        } else {
            out[k++] = c;
        }
    }

    out[k++] = COMM_ESCAPE;
    out[k++] = COMM_END;

    if ((err = send(fd, out, k, 0)) != k) {
        g_print("send_msg error: sent %d/%d octets\n", err, k);
        return -1;
    }

    return len;
}

void print_stat(void)
{
    gint i;

    msg_stat.stop = time(NULL);
    guint elapsed = msg_stat.stop - msg_stat.start;
    if (elapsed == 0)
        elapsed = 1;

    g_print("Messages in %d s: rx: %d (%d/s) tx: %d (%d/s)\n", 
            elapsed, msg_stat.rx, msg_stat.rx/elapsed, 
            msg_stat.tx, msg_stat.tx/elapsed);

    for (i = 1; i < NUM_MESSAGES; i++) {
        g_print("  %2d: rx: %d (%d/s) tx: %d (%d/s)\n", i,
                msg_stat.rxtype[i], msg_stat.rxtype[i]/elapsed,
                msg_stat.txtype[i], msg_stat.txtype[i]/elapsed);
    }
}

gpointer client_thread(gpointer args)
{
    SOCKET comm_fd;
    gint n;
    struct sockaddr_in node;
    struct message input_msg;
    static guchar inbuf[1000];
    fd_set read_fd, fds;
    gint old_port = get_port();

    for ( ; *((gboolean *)args) ; )   /* exit loop when flag is cleared */
    {
        gulong tmp_node_addr;

        if (node_ip_addr == 0 && this_is_shiai() == FALSE) {
            tmp_node_addr = nodescan(my_ip_address);
            if (tmp_node_addr == 0) {
                g_usleep(1000000);
                continue;
            }
            node_ip_addr = tmp_node_addr;
        } else {
            tmp_node_addr = node_ip_addr;
        }

        if ((comm_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
            perror("serv socket");
            g_print("CANNOT CREATE SOCKET (%s:%d)!\n", __FUNCTION__, __LINE__);
            g_thread_exit(NULL);    /* not required just good pratice */
            return NULL;
        }
#if 0
        int nodelayflag = 1;
        if (setsockopt(comm_fd, IPPROTO_TCP, TCP_NODELAY, 
                       (void *)&nodelayflag, sizeof(nodelayflag))) {
            g_print("CANNOT SET TCP_NODELAY\n");
        }
#endif
        memset(&node, 0, sizeof(node));
        node.sin_family      = AF_INET;
        node.sin_port        = htons(get_port());
        node.sin_addr.s_addr = tmp_node_addr;

        if (connect(comm_fd, (struct sockaddr *)&node, sizeof(node))) {
            closesocket(comm_fd);
            g_usleep(1000000);
            continue;
        }

        g_print("Connection OK.\n");

        /* send something to keep the connection open */
        input_msg.type = MSG_DUMMY;
        input_msg.sender = my_address;
        input_msg.u.dummy.application_type = application_type();
        send_msg(comm_fd, &input_msg);

        connection_ok = TRUE;

        FD_ZERO(&read_fd);
        FD_SET(comm_fd, &read_fd);

        while (tmp_node_addr == node_ip_addr) {
            struct timeval timeout;
            gint r;
			
            if (old_port != get_port()) {
                g_print("oldport=%d newport=%d\n", old_port, get_port());
                old_port = get_port();
                break;
            }

            fds = read_fd;
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;

            r = select(comm_fd+1, &fds, NULL, NULL, &timeout);

            g_static_mutex_lock(&send_mutex);
            if (msg_queue_put != msg_queue_get) {
                send_msg(comm_fd, &msg_to_send[msg_queue_get]);
                msg_queue_get++;
                if (msg_queue_get >= MSG_QUEUE_LEN)
                    msg_queue_get = 0;
            }
            g_static_mutex_unlock(&send_mutex);

            if (r <= 0)
                continue;

            if (FD_ISSET(comm_fd, &fds)) {
                struct message m;
                static gint ri = 0;
                static gboolean escape = FALSE;
                static guchar p[512];
                                
                if ((n = recv(comm_fd, inbuf, sizeof(inbuf), 0)) > 0) {
                    gint i;
                    g_static_mutex_lock(&rec_mutex);
                    for (i = 0; i < n; i++) {
                        guchar c = inbuf[i];
                        if (c == COMM_ESCAPE) {
                            escape = TRUE;
                        } else if (escape) {
                            if (c == COMM_FF) {
                                if (ri < sizeof(p))
                                    p[ri++] = COMM_ESCAPE;
                            } else if (c == COMM_BEGIN) {
                                ri = 0;
                                memset(p, 0, sizeof(p));
                            } else if (c == COMM_END) {
				decode_msg(&m, p, ri);
                                if (msg_accepted(&m)) {
                                    rec_msgs[rec_msg_queue_put] = m;
                                    rec_msg_queue_put++;
                                    if (rec_msg_queue_put >= MSG_QUEUE_LEN)
                                        rec_msg_queue_put = 0;
                                }
                            } else {
                                g_print("Wrong char 0x%02x after esc!\n", c);
                            }
                            escape = FALSE;
                        } else if (ri < sizeof(p)) {
                            p[ri++] = c;
                        }
                    }
                    g_static_mutex_unlock(&rec_mutex);
                } else 
                    break;
            }
        }

        connection_ok = FALSE;

        closesocket(comm_fd);

        g_print("Connection NOK.\n");
    }

    g_thread_exit(NULL);    /* not required just good pratice */
    return NULL;
}

static const guint crctab[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419,
    0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4,
    0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07,
    0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856,
    0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4,
    0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3,
    0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a,
    0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599,
    0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190,
    0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f,
    0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0x0f00f934, 0x9609a88e,
    0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed,
    0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3,
    0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a,
    0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5,
    0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010,
    0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17,
    0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6,
    0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615,
    0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 0xf00f9344,
    0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a,
    0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1,
    0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c,
    0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef,
    0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe,
    0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31,
    0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c,
    0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b,
    0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1,
    0x18b74777, 0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
    0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278,
    0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7,
    0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66,
    0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605,
    0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8,
    0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b,
    0x2d02ef8d
};

gint pwcrc32(const guchar *str, gint len)
{
    gint i;
    guint crc = 0xffffffff;

    if (!str)
        return 0;

    for (i = 0; i < len; i++)
        crc = (crc >> 8) ^ crctab[(crc ^ str[i]) & 0xff];
    crc ^= 0xffffffff;
    return crc;
}
