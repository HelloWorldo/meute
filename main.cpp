#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#define MAX_ARG 10

enum MODE
{
    MODE_VERBOSE=   0x1,
    MODE_DEBUG=     0x2,
    MODE_FOLLOW=    0x4,
    MODE_POST=      0x8,
    MODE_4=         0x10
};
int mode=0;
struct info
{
    int id;
    int status;
    int length;
    char data[512];
};
struct data
{
    const char *readptr;
    int sizeleft;
};
int rand_a_b(int a, int b){
    return rand()%(b-a) +a;
}
void populate(char * data_dest, char * data_src)
{
    int i=0;
    char digits[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    char aChar = digits[i];
    while(1)
    {
        if(data_src[i]=='X')
        {
            data_dest[i]=digits[rand_a_b(0,9)];
        }
        else
        {
            data_dest[i]=data_src[i];
        }
        if(data_src[i]=='\0') return;
        i++;
    }
}
size_t header_callback(char *buffer, size_t size,
                              size_t nitems, void *userdata)
{
  /* received header is nitems * size long in 'buffer' NOT ZERO TERMINATED */
  /* 'userdata' is set with CURLOPT_WRITEDATA */
    int status;
    char tmp[256];
    if(memcmp(buffer, "HTTP/1.1 ",9)==0)
    {
        sscanf(buffer, "HTTP/1.1 %i %s", &status, &tmp);
        ((struct info*)userdata)->status=status;
    }
    ((struct info*)userdata)->length+=nitems * size;
    return nitems * size;
}
size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    ((struct info*)userdata)->length+=(nmemb * size);
    if(mode&MODE_VERBOSE) printf("%s\n", ptr);
    return nmemb * size;
}
CURL* init(CURLM *cm, char *url, struct info* pp, struct curl_slist* header)
{
    CURL *eh = curl_easy_init();
    //URL scheme://host:port/path
    curl_easy_setopt(eh, CURLOPT_URL, url);
    //Write callback (defaut - fwrite)
    curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_callback);
    //Header processing
    curl_easy_setopt(eh, CURLOPT_HEADERFUNCTION, header_callback);
    //Set private data (pointer to our info struct)
    curl_easy_setopt(eh, CURLOPT_PRIVATE, pp);
    curl_easy_setopt(eh, CURLOPT_HEADERDATA, pp);
    curl_easy_setopt(eh, CURLOPT_WRITEDATA, pp);
    
    //Follow redirections
    if(mode&MODE_FOLLOW)
    {
        curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, 1L);
    }
    
    //POST
    if(mode&MODE_POST)
    {
        curl_easy_setopt(eh, CURLOPT_POST, 1L); 
        curl_easy_setopt(eh, CURLOPT_POSTFIELDS, pp->data);
    }
    
    //Header
    if(header)
    {
        curl_easy_setopt(eh, CURLOPT_HTTPHEADER, header);
    }
    curl_multi_add_handle(cm, eh);
    return eh;
}
int main( int argc, char** argv)
{
    unsigned long time_global_start=clock(), time_global_end=0, time_global_bench=0;
    unsigned long time_processing=0, time_init=0;
    int requests=1;
    int concurrency=1;
    char url[256];
    FILE* post_file;
    short post_file_path_id=-1;
    char post_data[256];
    srand(time(0));
    //Options
  	for(int i=1;i<argc;i++)
  	{
        if(strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
        {
            mode|=MODE_VERBOSE;
        }
        else if(strcmp(argv[i], "-c") == 0)
        {
            concurrency= atoi(argv[i+1]);
            i++;
        }
        else if(strcmp(argv[i], "-n") == 0)
        {
            requests=atoi(argv[i+1]);
            i++;
        }
        else if(strcmp(argv[i], "-d") == 0)
        {
            mode|=MODE_DEBUG;
            i++;
        }
        else if(strcmp(argv[i], "-f") == 0)
        {
            mode|=MODE_FOLLOW;
            i++;
        }
        else if(strcmp(argv[i], "-p") == 0)
        {
            mode|=MODE_POST;
            post_file_path_id=i+1;
            post_file = fopen(argv[post_file_path_id], "r");
            if (!post_file)
            {
                printf("\nFile '%s' was not found or locked.\n", post_file);
                return 1;
            }
            i++;
        }
        else
        {
            strcpy(url, argv[i]);
        }
    }
    printf("url: %s\nn: %i\nc: %i\n", url, requests, concurrency);
    //Header
    struct curl_slist *header = 0;
    header = curl_slist_append(header, "Accept: */*");
    header = curl_slist_append(header, "Content-Type: application/json");
    header = curl_slist_append(header, "User-Agent: libcurl-meute/1.0");
    //Disable Expect : 100
    //Using POST with HTTP 1.1 implies the use of a "Expect: 100-continue" header
    //header = curl_slist_append(header, "Expect:");
    
    //Use chunk encoding (avoid to specify the size)
    header = curl_slist_append(header, "Transfer-Encoding: chunked");
    
    if(mode&MODE_POST)
    {
        printf("POST file: %s\n", argv[post_file_path_id] );
    }
    
    CURLM * curl=0;
    CURLMsg * result=0;
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_multi_init();
    if(!curl) return 1;

    //Info big allocation
    struct info *infos = (struct info*)malloc(sizeof(struct info)*requests);
    if(mode&MODE_DEBUG) printf("Memory alloc: %i\n", sizeof(struct info)*requests);
    
    double time_nl=0,time_c=0, time_ac=0, time_pt=0, time_st=0, time_total=0, time_r=0;
    int running_requests=-1;
    int queued_requests=-1;
    int total_requests=0;
    unsigned int success=0;
    unsigned int success2xx=0;
    unsigned int failure=0;
    unsigned int bytes=0;
    int max_running=0;
	
    //Read POST data from file
    if(mode&MODE_POST)
    {
        unsigned int i;
        for(i=0;;i++)
        {
            post_data[i]=fgetc(post_file);
            if(post_data[i]==EOF) break;
        }
        post_data[i]='\0';
        if(mode&MODE_DEBUG) printf("POST data: %s\n", post_data);
	}
    
    time_global_bench = clock();
    //Main test loop
    while(total_requests<requests || running_requests || queued_requests)
    {
        //Create a new connection with private info data
        if(running_requests<concurrency && total_requests<requests)
        {
            long init_time=clock();
            //Seek
            struct info* r = infos;
            r+=total_requests;
            //Set info data
            r->id=total_requests;
            r->status=0;
            r->length=0;
            //Populate with random values;
            populate(r->data, post_data);
            init(curl, url, r, header);
            total_requests++;
            time_init+=clock()-init_time;
        }
        //Keep running
        curl_multi_perform(curl, &running_requests);
        if(running_requests>max_running) max_running=running_requests;
    
        long processing_time=clock();
        //Results processing
        if(result = curl_multi_info_read(curl, &queued_requests))
        {
            if (result->msg == CURLMSG_DONE)
            {
                CURL *e = result->easy_handle;
                //Retrieve private info data
                struct info *data;
                curl_easy_getinfo(e, CURLINFO_PRIVATE, &data);
                   
                //Time
                double t=0;
                int i = 0;
                curl_easy_getinfo(e, CURLINFO_NAMELOOKUP_TIME, &t);time_nl+=t;
                curl_easy_getinfo(e, CURLINFO_CONNECT_TIME, &t);time_c+=t;
                curl_easy_getinfo(e, CURLINFO_APPCONNECT_TIME, &t);time_ac+=t;
                curl_easy_getinfo(e, CURLINFO_PRETRANSFER_TIME, &t);time_pt+=t;
                curl_easy_getinfo(e, CURLINFO_STARTTRANSFER_TIME, &t);time_st+=t;
                curl_easy_getinfo(e, CURLINFO_TOTAL_TIME, &t);time_total+=t;
                curl_easy_getinfo(e, CURLINFO_REDIRECT_TIME, &t);time_r+=t;

                if(mode&MODE_DEBUG) printf("id: %i access: %p status: %i length: %i\n",data->id, data, data->status, data->length);
                
                //Error
                if(result->data.result!=0)
                {
                    fprintf(stderr, "Result: %d - %s\n",result->data.result,curl_easy_strerror(result->data.result));
                    failure++;
                }
                else
                {
                    //Success
                    success++;
                    if(data->status>=200 && data->status<300)
                    {
                        success2xx++;
                    }
                }
                //Length
                bytes+=data->length;
                //Cleanup
                curl_multi_remove_handle(curl, e);
                curl_easy_cleanup(e);
            }
            else
            {
                fprintf(stderr, "error: CURLMsg (%d)\n", result->msg);
                failure++;
            }
        }
        time_processing+=processing_time;
    }
    time_global_end = clock() - time_global_bench;
    
    //Check the final data
    if(mode&MODE_DEBUG)
    {
        struct info* in=infos;
        int itno;
        for(itno=0; itno<requests; itno++,in++)
        {
            printf("%i access: %p status: %i length: %i\n",in->id, in, in->status, in->length);
        }
    }
    
    //Results
    printf("Data transfered: %d bytes\n", bytes);
    printf("Success: %i (non 2xx: %i) Fail: %i\n", success, requests-success2xx-failure, failure);
    printf("Average time per request: %.3f ms\n", time_total/total_requests*1000);
    int o =  time_global_end *1000 /CLOCKS_PER_SEC;
    printf("RPS: %.3f (max: %d)\n\n", (float)total_requests/o*1000, max_running);
    printf("NAMELOOKUP\t\t%.3f\t%.1f%%\n\
|-CONNECT\t\t%.3f\t%.1f%%\n\
|-|-APPCONNECT\t\t%.3f\t%.1f%%\n\
|-|-|-PRETRANSFER\t%.3f\t%.1f%%\n\
|-|-|-|-STARTTRANSFER\t%.3f\t%.1f%%\n\
|-|-|-|-|-TOTAL\t\t%.3f\n\
|-|-|-|-|-REDIRECT\t%.3f\n", time_nl/total_requests*1000, time_nl/time_total*100,time_c/total_requests*1000,time_c/time_total*100, time_ac/total_requests*1000,time_ac/time_total*100, time_pt/total_requests*1000, time_pt/time_total*100,time_st/total_requests*1000, time_st/time_total*100, time_total/total_requests*1000, time_r/total_requests*1000);
    
    if(mode&MODE_DEBUG)
    {
        int msec = time_init *1000 /CLOCKS_PER_SEC;
        printf("Init time: %d s %d ms\n", msec/1000, msec%1000 );
        msec = (time_global_bench - time_global_start) *1000 /CLOCKS_PER_SEC;
        printf("Pre init time: %d s %d ms\n", msec/1000, msec%1000 );
        msec = time_processing *1000 /CLOCKS_PER_SEC;
        printf("Processing time: %d s %d ms\n", msec/1000, msec%1000 );
    }
    printf("Total time: %d s %d ms\n", o/1000, o%1000 );
    curl_slist_free_all(header);
    curl_multi_cleanup(curl);
    curl_global_cleanup();
    if(post_file) fclose(post_file);
    free(infos);
    return 0;
}
