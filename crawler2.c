#include "bloom2.h"
#include "url2.h"
#include "http2.h"

#define MAX_QUEUE_SIZE 2000000

queue < Url * >url_queue;
struct epoll_event ev, events[2560];	//ev用于注册事件，events数组用于回传要处理的事件
int epfd;
int url_num = 1;
int page_num = 0;

void putlinks2queue(char *links[], int count);
void addurl2queue(Url * url);

int main(int argc, char *argv[]){
	int i, n;
	if (argc != 4) {
		printf
		    ("Usage: ./crawler domain port url.txt\n"
		     "For example:./crawler news.sohu.com 80 url.txt\n");
		return 0;
	}
	chdir("pagecrawler");
    FILE* logfd = fopen("log.txt", "w+");
    if (logfd == NULL) {
        printf("log文件创建失败\n");
        exit(1);
    }
	url_num = argc - 3;
	putlinks2queue(argv + 1, url_num);	/*把用户命令行提供的link放入待爬取url队列 */

	epfd = epoll_create(2560);	//生成用于处理accept的epoll专用文件描述符，最多监听2560个事件
	n = url_queue.size() < 200 ? url_queue.size() : 200;	/*最多同时放出去2500条socket连接 */
	for (i = 0; i < n; i++) {
		Url *url = url_queue.front();
		int sockfd;
		int rect = buildConnect(&sockfd, url->ip);	/*发出connect请求 */
		setnoblocking(sockfd);	/*设置sockfd非阻塞模式 */
		rect = sendRequest(url, sockfd);	/*发送http request */
		Ev_arg *arg = (Ev_arg *) calloc(sizeof(Ev_arg), 1);
		arg->url = url;
		arg->fd = sockfd;
		ev.data.ptr = arg;
		ev.events = EPOLLIN | EPOLLET;	//设置要处理的事件类型。可读，边缘触发
		printf("将要把%d: [%s%s]放入epoll\n", sockfd, url->domain, url->path);
		epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);	//注册ev
		url_queue.pop();
	}

    int ask_num = 0;
	while (1) {
        clock_t start, finish;
        double  duration;
        start = clock();

		n = epoll_wait(epfd, events, 2560, 100000);	/*等待sockfd可读，即等待http response */
		if((n == 0) && (url_queue.size() == 0)){
            finish = clock();
            duration = (double)(finish - start) / CLOCKS_PER_SEC;
            printf("epoll_wail call : %d times\n \
                        %d pages has been crawled.\n \
                            Queue Size : %d\n", ++ask_num, page_num, url_queue.size());
            fprintf(logfd, "The %d ask processed in %f seconds\n \
                            %d epoll_wait has been processed.\n \
                            Queue Size : %d\n", ++ask_num, duration, n, url_queue.size());
            return 0;
		}
		printf("有%d个sockfd准备就绪\n", n);
		/*
		for (i = 0; i < n; ++i) {
			Ev_arg *arg = (Ev_arg *) (events[i].data.ptr);
			recvResponse(arg);
		}
		*/
        for (i = 0; i < n; ++i) {
			Ev_arg *arg = (Ev_arg *) (events[i].data.ptr);
            Url *url = arg->url;
            int fd = arg->fd;

            int i, read_length, need, ll = 0;
            memset(buf, 0, MAX_RECV_SIZE);
            while (1) {
                need = sizeof(buf) - 1 - ll;
                read_length = read(fd, buf, need);
                if (read_length < 0) {
                    if (errno == EAGAIN) {
                        usleep(1000);
                        continue;
                    } else {
                        fprintf(stderr, "读取http响应发生错误\n");
                        //freeUrl(url);
                        break;
                    }
                } else if (read_length == 0) {	/*读取http响应这完毕 */
                    break;
                } else {
                    char *fn = link2fn(url);	/*以url作为文件名，斜线转换为下划线 */
                    if((strstr(buf, "404 Not") == NULL)  && (strstr(buf, "301 Moved") == NULL)
                        && (strstr(buf, "403 Forb") == NULL) && (strstr(fn, ".jpg") == NULL)){
                        /*以只写方式打开html文件 */
                        int htmlfd = open(fn, O_WRONLY | O_CREAT | O_APPEND, 0644);
                        if (htmlfd < 0) {
                            fprintf(stderr,"函数recvResponse()中%s文件打开失败\n%s\t%s\n",
                                fn, url->domain, url->path);
                            //freeUrl(url);
                            break;
                        }
                        else{
                            write(htmlfd, buf, read_length);
                            extractLink2(fn);
                            close(htmlfd);
                            printf("Has crawled %d pages.\n", ++page_num);
                        }
                    }
                    free(fn);
                    break;
                }
            }
            close(fd);

            connect_pending--;

            int add_connect = url_queue.size() < (200 - connect_pending) ? url_queue.size() : (200 - connect_pending);
            for (i = 0; i < add_connect; ++i) {
                if (url_queue.empty()){
                    //usleep(1000);
                    exit(15);
                    //continue;
                }
                Url *url = url_queue.front();
                //url_queue.pop();
                int sockfd;
                //printf("将要发起连接请求\n");
                int rect = buildConnect(&sockfd, url->ip);	/*发出connect请求 */
                setnoblocking(sockfd);	/*设置sockfd非阻塞模式 */
                rect = sendRequest(url, sockfd);	/*发送http request */
                Ev_arg *arg = (Ev_arg *) calloc(sizeof(Ev_arg), 1);
                arg->url = url;
                arg->fd = sockfd;
                ev.data.ptr = arg;
                ev.events = EPOLLIN | EPOLLET;	//设置要处理的事件类型。可读，边缘触发
                printf("将要把%d: [%s%s]放入epoll\n", sockfd, url->domain, url->path);
                epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);	//注册ev
                url_queue.pop();
            }
        }
        fclose(logfd);
        logfd = fopen("log.txt", "a");

        finish = clock();
        duration = (double)(finish - start) / CLOCKS_PER_SEC;
        fprintf(logfd, "The %d ask processed in %f seconds\n \
               %d epoll_wait has been processed.\n \
                Queue Size : %d\n", ++ask_num, duration, n, url_queue.size());
	}
	close(epfd);
}

/*把超链接放入待爬取url队列*/
void putlinks2queue(char *links[], int count){
    int i = 0;
	for (i = 0; i < count; i++) {
		if (links[i] == NULL)
			continue;
		char *h = (char *)calloc(MAX_LINK_LEN, 1);
		char *r = (char *)calloc(MAX_LINK_LEN, 1);
		char *iipp = (char *)calloc(20, 1);
		pretreatLink(links[i]);
		if (links[i] == NULL)
			continue;
		getHRfromlink(links[i], h, r);

		if(strcmp(h, "news.sohu.com") != 0){
            free(h);
            free(r);
            free(iipp);
            continue;
		}
		else{
            if (bloomPath(r)) {	/*path出现过，则忽略此link */
				free(h);
				free(r);
				free(iipp);
				continue;
			} else {	/*path没有出现 */
				Url *tmp = (Url *) calloc(1, sizeof(Url));
				tmp->domain = h;
				tmp->path = r;
                tmp->ip = "127.0.0.1";
                addurl2queue(tmp);
                continue;
            }
        }
    }
}

/*把url放入待爬取队列*/
void addurl2queue(Url * url)
{
	if (url == NULL || url->domain == NULL || strlen(url->domain) == 0
        || url->ip == NULL || strlen(url->ip) == 0) {
		fprintf(stderr,
			"Url内容不完整。domain=%s\tpath=%s\tip=%s\n",
			url->domain, url->path, url->ip);
		exit(1);
	}
	//fprintf(stderr,"Url内容完整。domain=%s\tpath=%s\tip=%s\n",url->domain,url->path,url->ip);
	if (url_queue.size() >= MAX_QUEUE_SIZE){	/*如果队列已满，就忽略此url */
	    printf("队列满\n");
        free(url);
		return;
	}
	url_queue.push(url);
	printf("%s%s已放入待爬取队列\n",url->domain,url->path);
}

