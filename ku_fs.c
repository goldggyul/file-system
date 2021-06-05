#include <stdlib.h>
#include <stdio.h>
#define BLOCK_SIZE 4096 // 블럭의 한 크기는 4KB
#define BLOCK_COUNT 64 // 블럭 64개까지 가능
#define PARTITION_SIZE BLOCK_SIZE*BLOCK_COUNT // 파티션 크기는 256KB
#define DATA_BLOCK_COUNT BLOCK_COUNT-8 // 가능한 데이터블록 개수
#define INODE_COUNT 80 // 가능한 inode 개수
typedef unsigned int uint;


void *disk;
unsigned char *ibmap;
unsigned char *dbmap;
void *data_region;
void *inode_table;
int root_inum;

// 256Byte
typedef struct inode{ //file information
    uint fsize; //4B, bytes in this file
    uint blocks; //4B, blocks allocated to this file
    uint pointer[12]; //48B, direct pointers
    uint dummy[50]; //200B, unused, no indirect pointers
} inode;

typedef struct dentry{
    unsigned char inum;
    char name[3];
} dentry;

void print_disk(void){
    int prior=-1;
    for(int i=0;i<PARTITION_SIZE;i++){
        if(i==BLOCK_SIZE){
            printf("%d:ibmap 시작\n", i);
        }else if(i==BLOCK_SIZE*2){
            printf("%d:dbmap 시작\n", i);
        }else if(i==BLOCK_SIZE*3){
            printf("%d:inode table 시작\n", i);
        }else if(i==BLOCK_SIZE*8){
            printf("%d:data region 시작\n", i);
        }
        unsigned char val=*((unsigned char *)disk+i);
        if(val !=0){
            if(prior!=-1 &&prior==val){
                printf("%d: %.2x ", i,val);
                continue;
            }else{
                printf("\n");
                printf("%d: %.2x ", i,val);
                prior=val;
            }
        }else{
            if(prior!=-1){
                printf("\n");
                prior=-1;
            }
        }
    }
}
int get_empty(unsigned char *bmap, int count){
    // 비트맵(for inode or data block)을 보고
    // 사용가능한(즉 0인)비트 중 가장 앞에 있는 변호 번환
    // bmap도 업데이트
    // 꽉찼을 시 -1
    for (int i=0;i<count;i++){
        if((*(bmap+(i/8))&(1<<(7-(i%8))))==0){
            printf("%d 중 %d번째 비어있음\n", count, i);
            //update i-bmap
            *(bmap+(i/8))|=(1<<(7-(i%8)));
            return i;
        }
    }

    return -1;
    
}

void print_inode(inode* inode){
    printf("=========inode정보==============\n");
    printf("1. 총 bytes: %d\n", inode->fsize);
    printf("2. 총 블럭수: %d\n", inode->blocks);
    printf("3. direct pointers\n");
    for(int i=0;i<12;i++){
        printf("%d:%d/ ", i, inode->pointer[i]);
    }
    printf("\n===============================\n");
}

int init(void){
    // allocate memory
    disk=calloc(PARTITION_SIZE,1);
    ibmap=disk+BLOCK_SIZE;
    dbmap=disk+BLOCK_SIZE*2;
    data_region=disk+BLOCK_SIZE*8;
    inode_table=disk+BLOCK_SIZE*3;
    printf("디스크 주소 : 0x%p\n", disk);
    printf("ibmap 주소 : 0x%p\n", ibmap);
    printf("dbmap 주소 : 0x%p\n", dbmap);
    printf("data_region 주소 : 0x%p\n", data_region);
    printf("inode_table : 0x%p\n", inode_table);
    
    // print
    printf("가능한 데이터블록 개수 : %d\n", DATA_BLOCK_COUNT);
    printf("가능한 inode 개수 : %d\n", INODE_COUNT);
    
    // i-bmap reserved bits (0,1)
    *ibmap|=(8+4)<<4;
    
    // root directory -> 2
    root_inum=get_empty(ibmap,INODE_COUNT);
    if(root_inum==-1){
        printf("No space\n");
        return -1;
    }
    printf("root의 inum: %d\n", root_inum);
    
    // Update root inode contents
    inode *root_inode=(inode *)inode_table+root_inum;
    // 1. fsize : 80개의 inodes 가능, 각 파일을 표현하기 위해 inum(1B)/Name(3B) 필요
    root_inode->fsize=INODE_COUNT*sizeof(dentry);
    // 2. (direct) pointer to data block
    int root_dnum=get_empty(dbmap, DATA_BLOCK_COUNT);
    if(root_dnum==-1){
        printf("No space\n");
        return -1;
    }
    root_inode->pointer[root_inode->blocks]=root_dnum;
    root_inode->blocks=1;
    // print information of root inode
    print_inode(root_inode);
    // 전체 디스크 내용 출력
    //print_disk();
    return 0;
}

void fin(FILE *fd){
    if(fd) fclose(fd);
    if(disk) free(disk);
}

int search_name(char* name, int flag){
    // 전체 파일에서 name과 같은 파일 이름 찾음
    inode *root_inode=(inode *)inode_table+root_inum;
    for(int i=0;i<root_inode->blocks;i++){
        // 데이터 블럭 주소 찾고, 각 entry 찾음
//        printf("%d 번 데이터블럭\n", root_inode->pointer[i]);
//        printf("%p\n",data_region+(root_inode->pointer[i])*BLOCK_SIZE);
        dentry *entry=(dentry *)(data_region+(root_inode->pointer[i])*BLOCK_SIZE);
        for(int j=0;j<16;j++){//가능한 inode 16개
            printf("inum:%d 파일명:%s\n", entry[j].inum, entry[j].name);
            if(entry[j].inum==0)
                continue;
            if(entry[j].name[0]==name[0] && entry[j].name[1]==name[1]){
                int inum=entry[j].inum;
                printf("찾은 파일 정보\n");
                print_inode((inode *)inode_table+inum);
                if(flag==1){
                    // delete
                    entry[j].inum=0;
                    printf("deleted\n");
                }
                // 파일 찾음 -> inum 반환
                return inum;
            }
        }
    }
    return -1;
}

int write_file(char fname[3], int size){
    if(search_name(fname,0)!=-1){
        // 존재하는 파일
        printf("Already exists\n");
        return 1;
    }
    // 파일 내용 -> 첫번째 알파벳을 사이즈만큼 저장
    int inum=get_empty(ibmap, INODE_COUNT);
    // No space
    if(inum==-1){
        printf("No space\n");
        return 1;
    }
    
    // Update inode contents
    inode *file_inode=(inode *)inode_table+inum;
    // 1. fsize : 80개의 inodes 가능, 각 파일을 표현하기 위해 inum(1B)/Name(3B) 필요
    file_inode->fsize=size;
    file_inode->blocks=size/BLOCK_SIZE+1;
    
    for(int i=0;i<file_inode->blocks;i++){
        int dblock=get_empty(dbmap,DATA_BLOCK_COUNT);
        // No space
        if(dblock==-1){
            printf("No space\n");
            return 1;
        }
        printf("%d번 데이터블럭 할당\n", dblock);
        // 2. (direct) pointer to data block (not real 'pointer')
        file_inode->pointer[i]=dblock;
    }
    // print information of file inode
    print_inode(file_inode);
    
    // 데이터 넣기
    for(int i=0;i<size;i++){
        *((unsigned char*)data_region+file_inode->pointer[i/BLOCK_SIZE]*BLOCK_SIZE+(i%BLOCK_SIZE))=fname[0];
    }
    
    // 루트의 inode 리스트에 비어있는, 즉 inum이 0인 곳에 지금 할당된 새로운 파일 연결
    // inode 리스트에 블럭 꽉찼으면 새로운 블럭 할당
    inode *root_inode=(inode *)inode_table+root_inum;
    for(int i=0;i<root_inode->blocks;i++){
        // 데이터 블럭 주소 찾고, 각 entry 찾음
        printf("%d 번 데이터블럭\n", root_inode->pointer[i]);
        dentry *entry=(dentry *)(data_region+(root_inode->pointer[i])*BLOCK_SIZE);
        for(int j=0;j<16;j++){//가능한 inode 16개
            printf("inum:%d 파일명:%s\n", entry[j].inum, entry[j].name);
            if(entry[j].inum!=0)
                continue;
            entry[j].inum=inum;
            entry[j].name[0]=fname[0];
            entry[j].name[1]=fname[1];
            entry[j].name[2]=fname[2];
            printf("update -> inum:%d 파일명:%s\n", entry[j].inum, entry[j].name);
            break;
        }
    }
    return 0;
}

int read_file(char fname[3], int size){
    // 2. command - r
    // 사이즈만큼 읽음. offset은 무시
    //    Success
    //      • Do printf() file contents as much as min(fsize, size); printf(“\n”);
    //    Fail
    //      • printf(“No such file\n”);
    int inum=search_name(fname,0);
    if(inum==-1){
        printf("No such file\n");
        return -1;
    }
    inode *file_inode=(inode *)inode_table+inum;
    int min_size=(size<file_inode->fsize)?size:file_inode->fsize;
    for(int i=0;i<min_size;i++){
        printf("%c", *((unsigned char*)data_region+file_inode->pointer[i/BLOCK_SIZE]*BLOCK_SIZE+(i%BLOCK_SIZE)));
    }
    printf("\n");
    return 0;
}

int delete_file(char fname[3]){
    // 3. command - d
    // 해당 파일이 소유한 inode와 data-block에 대응하는 비트를
    // i-bmap과 d-bmap에서 0으로 변경
    // 또한, root directory에서 해당 파일의 inum을 0으로 변경
    //    Success: nothing
    //    Fail
    //      • printf(“No such file\n”)
    
    // search + root directory에서 inum을 0으로 변경 (두번째 인자 1)

    int inum=search_name(fname,1);


    if(inum==-1){
        printf("No such file\n");
        return -1;
    }
    // d-bmap에서 0으로 바꾸기
    inode *file_inode=(inode *)inode_table+inum;
    printf("지워아햘 inode 정보\n");
    print_inode(file_inode);
    for(int i=0;i<file_inode->blocks;i++){
        int block=file_inode->pointer[i];
        *(dbmap+(block/8))^=(1<<(7-(block%8)));
    }
    // i-bmap에서 0으로 바꾸기
    *(ibmap+(inum/8))&=(0xFF-(1<<(7-(inum%8))));
    return 0;
}

int main(int argc, const char * argv[]) {
    // Open file
    FILE *fd=NULL;
    if(argc != 2){
        printf("Wrong number of arguments\n");
        return 1;
    }
    fd=fopen(argv[1], "r");
    if(!fd){
        printf("Fail to open the input file\n");
        return 1;
    }
    
    // Initialization
    init();
    
    // Read input file
    // 파일 이름은 알파벳 두개, 널문자까지 세개 필요
    char fname[3], command;
    int size=0;
    while(fscanf(fd, "%s %c", fname, &command)!=EOF){
        printf("\n=========================================\n");
        printf("\n파일이름 : %s\n", fname);
        printf("명령어 : %c\n", command);
        if(command!='d'){
            // size in bytes
            if(fscanf(fd, "%d", &size)!=EOF){
                printf("크기 : %d\n", size);
            }
            else{
                return 1;
            }
        }
        
        if(command=='w'){
            write_file(fname, size);
        }else if(command=='r'){
            read_file(fname, size);
        }else if(command=='d'){
            delete_file(fname);
        }else{
            printf("No such command\n");
            return 1;
        }
        printf("=========================================\n");
        
    }
    print_disk();
    fin(fd);
    
    return 0;
}