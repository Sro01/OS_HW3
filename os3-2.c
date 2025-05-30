/*
	명령어
	gcc -Wall os3-2.c && cat tests3-2/test3.bin | ./a.out

	PAGE 크기:  32bit(4byte)
	FRAME 개수: 256
	PAS 크기:	32bit * 256 = 8KB

	Page Table도 PAS에서 프레임을 할당하여 저장, 관리

	PAS
	- Page Table Frame: 연속된 frame 8개 (Page Table 1개 당)
	- User Page: Frame 1개

	메모리 부족으로 완료된 경우: "Out of memory!!\n" 
	각 프로세스별 정보 출력: "** Process %03d: Allocated Frames=%03d PageFaults/References=%03d/%03d\n"
	각 프로세스별 페이지 테이블 출력: "%03d -> %03d REF=%03d\n" 
	모든 작업 종료 후, 최종 리포트: "Total: Allocated Frames=%03d Page Faults/References=%03d/%03d\n"
*/
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>

int PAGE_SIZE = 32, PAS_FRAMES = 256, VAS_PAGES = 64, L1PT_SIZE = 8, L2PT_SIZE = 8;

#define PAS_SIZE (PAGE_SIZE*PAS_FRAMES) // test3.bin : 32 * 245 = 8192B
#define VAS_SIZE (PAGE_SIZE*VAS_PAGES) 	// test3.bin : 32 * 64 = 2048B
#define PTE_SIZE (4) // sizeof(pte)

// test3.bin: 64 * 4 / 32 = 8 consecutive frames

#define PAGETABLE_FRAMES (VAS_PAGES * PTE_SIZE / PAGE_SIZE)

#define PAGE_INVALID (0)
#define PAGE_VALID (1)

#define MAX_REFERENCES (256)

#define MAX_PROCESS_NUM (10)

#define PT_ENTRIES (PAGE_SIZE / PTE_SIZE)

typedef struct {
	unsigned char frame; // allocated frame
	unsigned char vflag; // valid-invalid bit
	unsigned char ref;	 // references bit
	unsigned char pad;	 // padding
} pte; // Page Table Entry (total 4Bytes)

typedef struct {
	int pid;
	int ref_len; // Less than 255
	unsigned char *references;
	
	pte *l1_pt; // ref_len * 4byte 크기
	int allocated_frame_cnt;
	int pagefault_cnt;
	int references_cnt;

	int ref_index;
} process;

unsigned char *pas; // 물리 메모리
int free_frame_index = 0;

process *process_list[MAX_PROCESS_NUM];
int pl_index = 0;

bool out_of_memory = false;

void print_result() {
	if (out_of_memory) {
		printf("Out of memory!!\n");
	}
	int total_allocated_frame = 0;
	int total_page_fault = 0;
	int total_ref = 0;

	for (int i = 0; i < pl_index; i++) {
		process *cur = process_list[i];
		if (cur == NULL) {
			return;
		}

		printf("** Process %03d: Allocated Frames=%03d PageFaults/References=%03d/%03d\n", 
			cur->pid, cur->allocated_frame_cnt, cur->pagefault_cnt, cur->references_cnt);
		
		// L1 순회
		for (int l1_index = 0; l1_index < PT_ENTRIES; l1_index++) {
			if (cur->l1_pt[l1_index].vflag == PAGE_VALID) {
				printf("(L1PT) %03d -> %03d\n", l1_index, cur->l1_pt[l1_index].frame);

				// L2 순회
				pte *l2_pt = (pte *)(pas + cur->l1_pt[l1_index].frame * PAGE_SIZE);
				for (int l2_index = 0; l2_index < PT_ENTRIES; l2_index++) {
					pte *l2_page = &l2_pt[l2_index];
					if (l2_page->vflag == PAGE_VALID) {
						int page_num = l1_index * PT_ENTRIES + l2_index;
						printf("(L2PT) %03d -> %03d REF=%03d\n", page_num, l2_page->frame, l2_page->ref);
					}
				}
			}
		}
		total_allocated_frame += cur->allocated_frame_cnt;
		total_page_fault += cur->pagefault_cnt;
		total_ref += cur->references_cnt;
	}
	printf("Total: Allocated Frames=%03d Page Faults/References=%03d/%03d\n", total_allocated_frame, total_page_fault, total_ref);

	return;
}

int get_free_frame() {
	if (free_frame_index >= PAS_FRAMES) {
		out_of_memory = true;
		// print_result();
		return -1;
	}
	else {
		return free_frame_index++;
	}
}

int handle_page_fault(process *process, pte *page) {
	if (page == NULL) return -1;

	/* 1. frame을 할당하기 */
	int frame = get_free_frame();
	if (frame == -1) return -1;
	
	
	page->frame = frame;
	page->vflag = PAGE_VALID;
	page->ref = 0;
	
	/* 2. pagefault_cnt 증가 */
	process->allocated_frame_cnt++;
	process->pagefault_cnt++;
	
	// printf("PF\n");
	return 0;
}

void allocate_frame_for_l1pt(process *process) {
	int frame = get_free_frame();
	if (frame == -1) {
		// printf("ERROR!\n");
		free(process->references);
		free(process);
		return;
	};

	process->l1_pt = (pte *)(pas + frame * PAGE_SIZE);
	process->allocated_frame_cnt++;
}

int do_memory_access(process *process, unsigned char page_num) {
	/* 1. 해당 프로세스의 L1 페이지 테이블 검색 */
	int l1_index = page_num / PT_ENTRIES;
	int l2_index = page_num % PT_ENTRIES;

	pte *l1_page = &process->l1_pt[l1_index];
	if (l1_page == NULL) {
		// printf("NULL\n");
		exit(1);
	}
	
	/* 2. L1 페이지에 L2 페이지의 물리 프레임이 할당되어 있지 않은 경우 */
	if (l1_page->vflag == PAGE_INVALID) {
		/* 2-1. l1_page fualt 처리 */
		if (handle_page_fault(process, l1_page) != 0) {
			// printf("ERROR: handle l1_page fault fail\n");
			return -1;
		}
	}
	
	pte *l2_pt = (pte *)(pas + l1_page->frame * PAGE_SIZE);
	pte *l2_page = &l2_pt[l2_index];
	if (l2_page->vflag == PAGE_INVALID) {
		if (handle_page_fault(process, l2_page) != 0) {
			// printf("ERROR: handle l2_page fault fail\n");
			return -1;
		}
	}

	// printf("PID: %02d PAGE: %02d FRAME: %02d FREE_F: %02d\n", process->pid, page_num, l1_page->frame, free_frame_index);
	
	/* 3. 해당 페이지에 이미 물리 프레임이 할당되어 있는 경우 */
	/* 3-1. 해당 프레임 번호로 접근 (해당 PTE에 references count 증가) */
	l2_page->ref++;
	process->references_cnt++;

	return 0;
}

void handle_memory_access() {
	/* 프로세스마다 번갈아가면서 메모리 접근 수행 */
	bool done = false;

	// printf("start\n");
	while (!done && !out_of_memory) {
		done = true;
		for (int i = 0; i< pl_index && !out_of_memory; i++) {
			process *cur = process_list[i];
			if (cur == NULL) {
				// printf("NULL\n");
				exit(1);
			}
			
			if (cur->ref_index < cur->ref_len) {
				done = false;
				
				// 페이지 번호
				unsigned char page_num = cur->references[cur->ref_index];
				// printf("PAGE %02d\n", page_num);
				
				if (do_memory_access(cur, page_num) != 0) {
					break;
				}
				
				if (!out_of_memory) {
					cur->ref_index++;
				}
			}
		}
	}
}

process* create_process(process *process_raw, unsigned char *references) {
	process *new_process = malloc(sizeof(process));
	new_process->pid = process_raw->pid;
	new_process->ref_len = process_raw->ref_len;
	new_process->references = malloc(sizeof(unsigned char) * new_process->ref_len);
	memcpy(new_process->references, references, new_process->ref_len);

	new_process->allocated_frame_cnt = 0;
	new_process->pagefault_cnt = 0;
	new_process->references_cnt = 0;

	new_process->ref_index = 0;

	/* 프로세스 로드 시, L1 PT를 위해 프레임을 하나 할당하고 초기화 */
	// 1. L1 프레임 할당
	allocate_frame_for_l1pt(new_process);

	// 2. L1 PT 초기화
	for (int i = 0; i < L1PT_SIZE; i++) {
		new_process->l1_pt[i].frame = 0;
		new_process->l1_pt[i].vflag = PAGE_INVALID;
		new_process->l1_pt[i].ref = 0;
		new_process->l1_pt[i].pad = 0;
	}

	return new_process;
}

void read_binary() {
	FILE *fp = stdin;

	/* 1. PAGE_SIZE, PAS_FRAMES, VAS_PAGES 입력 받기 */
	if (fread(&PAGE_SIZE, 4, 1, fp) != 1 || fread(&PAS_FRAMES, 4, 1, fp) != 1 || fread(&VAS_PAGES, 4, 1, fp) != 1) return;

	pas = (unsigned char *) calloc(PAS_SIZE, 1);

	process process_raw;
	unsigned char reference_raw;
	
	/* 2. Process PID, Ref_len 읽어오기 */
    while(fread(&process_raw, 8, 1, fp) == 1) { // pid(4byte), ref_len(4byte)
		// printf("PID:%d Ref_len: %d\n", process_raw.pid, process_raw.ref_len);
		int ref_len = process_raw.ref_len;
		unsigned char references[ref_len];

		/* 3. References 읽어오기 */
        for (int i = 0; i < ref_len; i++) { // reference의 개수만큼 reference를 읽어오기
            if (fread(&reference_raw, sizeof(reference_raw), 1, fp) == 1) {
				references[i] = reference_raw;
				// printf("%02d ",references[i]);
			}
        }

		process_list[pl_index] = create_process(&process_raw, references);
		pl_index++;
    }
}

int main() {
	read_binary();
	handle_memory_access();

	print_result();

    return 0;
}