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

int get_empty(unsigned char *bmap, int count){
    // 비트맵(for inode or data block)을 보고
    // 사용가능한(즉 0인)비트 중 가장 앞에 있는 번호 번환
    // bmap도 업데이트
    // 꽉찼을 시 -1
    for (int i=0;i<count;i++){
        if((*(bmap+(i/8))&(1<<(7-(i%8))))==0){
            //update i-bmap
            *(bmap+(i/8))|=(1<<(7-(i%8)));
            return i;
        }
    }
    return -1;
}

int init(void){
    // allocate memory
    disk=calloc(PARTITION_SIZE,1);
    ibmap=disk+BLOCK_SIZE;
    dbmap=disk+BLOCK_SIZE*2;
    data_region=disk+BLOCK_SIZE*8;
    inode_table=disk+BLOCK_SIZE*3;
    
    // i-bmap reserved bits (0,1)
    *ibmap|=(8+4)<<4;
    
    // root directory -> 2
    root_inum=get_empty(ibmap,INODE_COUNT);
    if(root_inum==-1){
        printf("No space\n");
        return -1;
    }    
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
        dentry *entry=(dentry *)(data_region+(root_inode->pointer[i])*BLOCK_SIZE);
        for(int j=0;j<16;j++){//가능한 inode 16개
            if(entry[j].inum==0)
                continue;
            if(entry[j].name[0]==name[0] && entry[j].name[1]==name[1]){
                int inum=entry[j].inum;
                if(flag==1){
                    // delete
                    entry[j].inum=0;
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
    if(size%BLOCK_SIZE==0){
        file_inode->blocks=file_inode->blocks-1;
    }
    
    for(int i=0;i<file_inode->blocks;i++){
        int dblock=get_empty(dbmap,DATA_BLOCK_COUNT);
        // No space
        if(dblock==-1){
            printf("No space\n");
            // Roll back
            file_inode->fsize=0;
            file_inode->blocks=0;
            for(int j=0;j<i;j++){
                // d-bmap에서 0으로 바꾸기
                int block=file_inode->pointer[i];
                file_inode->pointer[i]=0;
                *(dbmap+(block/8))^=(1<<(7-(block%8)));
            }
            // i-bmap에서 0으로 바꾸기
            *(ibmap+(inum/8))&=(0xFF-(1<<(7-(inum%8))));
            return 1;
        }
        // 2. (direct) pointer to data block (not real 'pointer')
        file_inode->pointer[i]=dblock;
    }

    // 데이터 넣기
    for(int i=0;i<size;i++){
        *((unsigned char*)data_region+file_inode->pointer[i/BLOCK_SIZE]*BLOCK_SIZE+(i%BLOCK_SIZE))=fname[0];
    }
    
    // 루트의 inode 리스트에 비어있는, 즉 inum이 0인 곳에 지금 할당된 새로운 파일 연결
    // inode 리스트에 블럭 꽉찼으면 새로운 블럭 할당
    inode *root_inode=(inode *)inode_table+root_inum;
    for(int i=0;i<root_inode->blocks;i++){
        // 데이터 블럭 주소 찾고, 각 entry 찾음
        dentry *entry=(dentry *)(data_region+(root_inode->pointer[i])*BLOCK_SIZE);
        for(int j=0;j<16;j++){//가능한 inode 16개
            if(entry[j].inum!=0)
                continue;
            entry[j].inum=inum;
            entry[j].name[0]=fname[0];
            entry[j].name[1]=fname[1];
            entry[j].name[2]=fname[2];
            break;
        }
    }
    return 0;
}

int read_file(char fname[3], int size){
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
    // search_name: search + root directory에서 inum을 0으로 변경 (두번째 인자 1)
    int inum=search_name(fname,1);

    if(inum==-1){
        printf("No such file\n");
        return -1;
    }
    // d-bmap에서 0으로 바꾸기
    inode *file_inode=(inode *)inode_table+inum;
    for(int i=0;i<file_inode->blocks;i++){
        int block=file_inode->pointer[i];
        *(dbmap+(block/8))^=(1<<(7-(block%8)));
    }
    // i-bmap에서 0으로 바꾸기
    *(ibmap+(inum/8))&=(0xFF-(1<<(7-(inum%8))));
    return 0;
}

void print_disk(void){
    for(int i=0;i<PARTITION_SIZE;i++){
        unsigned char val=*((unsigned char *)disk+i);
        printf("%.2x ", val);      
    }
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
        if(command!='d'){
            // size in bytes
            if(fscanf(fd, "%d", &size)==EOF){
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
    }
    print_disk();
    fin(fd);
    return 0;
}