#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>

#ifdef _WIN32
    #include <direct.h>  // for _mkdir
    #define mkdir _mkdir
#else
    #include <sys/types.h>
    #define mkdir(path, mode) mkdir(path, mode)
#endif

#define MAX_CHARS 256
#define MAGIC_SINGLE "HUFF"
#define MAGIC_MULTI  "MUF"

// ---------- Huffman 相关数据结构 ----------
typedef struct Node {
    unsigned char ch;
    long freq;
    struct Node *left, *right;
} Node;

long freq[MAX_CHARS] = {0};
char codes[MAX_CHARS][MAX_CHARS];
int code_len[MAX_CHARS];

Node* createNode(unsigned char ch, long freq, Node* left, Node* right) {
    Node* node = (Node*)malloc(sizeof(Node));
    node->ch = ch; node->freq = freq; node->left = left; node->right = right;
    return node;
}

Node* buildHuffmanTree() {
    Node* nodes[MAX_CHARS];
    int nodeCount = 0;
    for (int i = 0; i < MAX_CHARS; i++) {
        if (freq[i] > 0) nodes[nodeCount++] = createNode((unsigned char)i, freq[i], NULL, NULL);
    }
    if (nodeCount == 1) return nodes[0];
    while (nodeCount > 1) {
        int min1 = 0, min2 = 1;
        if (nodes[min1]->freq > nodes[min2]->freq) { int tmp = min1; min1 = min2; min2 = tmp; }
        for (int i = 2; i < nodeCount; i++) {
            if (nodes[i]->freq < nodes[min1]->freq) { min2 = min1; min1 = i; }
            else if (nodes[i]->freq < nodes[min2]->freq) { min2 = i; }
        }
        Node* new = createNode(0, nodes[min1]->freq + nodes[min2]->freq, nodes[min1], nodes[min2]);
        nodes[min1] = new;
        nodes[min2] = nodes[nodeCount-1];
        nodeCount--;
    }
    return nodes[0];
}

void generateCodes(Node* node, char* buffer, int depth) {
    if (!node) return;
    if (!node->left && !node->right) {
        buffer[depth] = '\0';
        strcpy(codes[node->ch], buffer);
        code_len[node->ch] = depth;
        return;
    }
    if (node->left) { buffer[depth] = '0'; generateCodes(node->left, buffer, depth+1); }
    if (node->right) { buffer[depth] = '1'; generateCodes(node->right, buffer, depth+1); }
}

void freeTree(Node* node) {
    if (!node) return;
    freeTree(node->left); freeTree(node->right); free(node);
}

// ---------- 单文件压缩核心（数据到内存）----------
unsigned char* huffmanCompressData(const unsigned char* data, long dataSize, long* outSize) {
    if (dataSize == 0) { *outSize = 0; return NULL; }
    memset(freq, 0, sizeof(freq));
    for (long i = 0; i < dataSize; i++) freq[data[i]]++;
    Node* root = buildHuffmanTree();
    char buf[MAX_CHARS] = {0};
    generateCodes(root, buf, 0);
    
    long bitCount = 0;
    for (long i = 0; i < dataSize; i++) bitCount += code_len[data[i]];
    *outSize = (bitCount + 7) / 8;
    unsigned char* compressed = (unsigned char*)malloc(*outSize);
    if (!compressed) { freeTree(root); *outSize = 0; return NULL; }
    
    unsigned char bitBuffer = 0;
    int bitsInBuffer = 0;
    long outIdx = 0;
    for (long i = 0; i < dataSize; i++) {
        char* code = codes[data[i]];
        for (int j = 0; j < code_len[data[i]]; j++) {
            if (code[j] == '1') bitBuffer |= (1 << (7 - bitsInBuffer));
            bitsInBuffer++;
            if (bitsInBuffer == 8) {
                compressed[outIdx++] = bitBuffer;
                bitBuffer = 0; bitsInBuffer = 0;
            }
        }
    }
    if (bitsInBuffer > 0) compressed[outIdx++] = bitBuffer;
    *outSize = outIdx;
    freeTree(root);
    return compressed;
}

// 单文件解压核心
unsigned char* huffmanDecompressData(const unsigned char* compressed, long compSize, long originalSize, const long freqTable[256]) {
    if (originalSize == 0) {
        unsigned char* empty = (unsigned char*)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    memcpy(freq, freqTable, sizeof(freq));
    Node* root = buildHuffmanTree();
    unsigned char* original = (unsigned char*)malloc(originalSize);
    if (!original) { freeTree(root); return NULL; }
    
    long written = 0;
    Node* cur = root;
    for (long i = 0; i < compSize && written < originalSize; i++) {
        unsigned char byte = compressed[i];
        for (int bit = 7; bit >= 0; bit--) {
            if (written >= originalSize) break;
            int b = (byte >> bit) & 1;
            if (b == 0) cur = cur->left;
            else cur = cur->right;
            if (!cur->left && !cur->right) {
                original[written++] = cur->ch;
                cur = root;
            }
        }
    }
    freeTree(root);
    return original;
}

// ---------- 单文件压缩/解压 ----------
void compressSingleFile(const char* inputFile, const char* outputFile) {
    FILE* fin = fopen(inputFile, "rb");
    if (!fin) { perror("Failed to open input file"); return; }
    fseek(fin, 0, SEEK_END);
    long fileSize = ftell(fin);
    rewind(fin);
    unsigned char* data = (unsigned char*)malloc(fileSize);
    if (!data) { fclose(fin); return; }
    fread(data, 1, fileSize, fin);
    fclose(fin);
    
    long compSize;
    unsigned char* compData = huffmanCompressData(data, fileSize, &compSize);
    free(data);
    if (!compData) return;
    
    FILE* fout = fopen(outputFile, "wb");
    if (!fout) { perror("Failed to create output file"); free(compData); return; }
    fwrite(MAGIC_SINGLE, 1, 4, fout);
    fwrite(freq, sizeof(long), MAX_CHARS, fout);
    fwrite(&fileSize, sizeof(long), 1, fout);
    fwrite(compData, 1, compSize, fout);
    fclose(fout);
    free(compData);
    printf("Compression done: %s -> %s (original %ld bytes, compressed %ld bytes)\n", inputFile, outputFile, fileSize, compSize);
}

void decompressSingleFile(const char* inputFile, const char* outputFile) {
    FILE* fin = fopen(inputFile, "rb");
    if (!fin) { perror("Failed to open compressed file"); return; }
    char magic[5] = {0};
    fread(magic, 1, 4, fin);
    if (strcmp(magic, MAGIC_SINGLE) != 0) {
        printf("Error: not a valid HUFF single file\n");
        fclose(fin); return;
    }
    long freqTable[MAX_CHARS];
    fread(freqTable, sizeof(long), MAX_CHARS, fin);
    long originalSize;
    fread(&originalSize, sizeof(long), 1, fin);
    fseek(fin, 0, SEEK_END);
    long compSize = ftell(fin) - (4 + sizeof(long)*MAX_CHARS + sizeof(long));
    rewind(fin);
    fseek(fin, 4 + sizeof(long)*MAX_CHARS + sizeof(long), SEEK_SET);
    unsigned char* compData = (unsigned char*)malloc(compSize);
    fread(compData, 1, compSize, fin);
    fclose(fin);
    
    unsigned char* origData = huffmanDecompressData(compData, compSize, originalSize, freqTable);
    free(compData);
    if (!origData) return;
    FILE* fout = fopen(outputFile, "wb");
    if (!fout) { perror("Failed to create output file"); free(origData); return; }
    fwrite(origData, 1, originalSize, fout);
    fclose(fout);
    free(origData);
    printf("Decompression done: %s -> %s\n", inputFile, outputFile);
}

// ---------- 多文件/目录收集 ----------
typedef struct {
    char* path;
    unsigned char* data;
    long size;
    unsigned char* compData;
    long compSize;
    long freqTable[256];
} FileEntry;

typedef struct {
    FileEntry* entries;
    int count;
    int capacity;
} FileList;

void initFileList(FileList* list) {
    list->count = 0; list->capacity = 10;
    list->entries = (FileEntry*)malloc(sizeof(FileEntry) * list->capacity);
}

void addFileToList(FileList* list, const char* path) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->entries = (FileEntry*)realloc(list->entries, sizeof(FileEntry) * list->capacity);
    }
    list->entries[list->count].path = strdup(path);
    list->entries[list->count].data = NULL;
    list->entries[list->count].size = 0;
    list->entries[list->count].compData = NULL;
    list->entries[list->count].compSize = 0;
    list->count++;
}

void freeFileList(FileList* list) {
    for (int i = 0; i < list->count; i++) {
        free(list->entries[i].path);
        if (list->entries[i].data) free(list->entries[i].data);
        if (list->entries[i].compData) free(list->entries[i].compData);
    }
    free(list->entries);
    list->count = 0;
}

void collectFiles(const char* dirPath, FileList* list, const char* baseDir) {
    DIR* dir = opendir(dirPath);
    if (!dir) return;
    struct dirent* entry;
    char fullPath[1024];
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, entry->d_name);
        struct stat st;
        if (stat(fullPath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                collectFiles(fullPath, list, baseDir);
            } else {
                addFileToList(list, fullPath);
            }
        }
    }
    closedir(dir);
}

// 递归创建多级目录（支持 / 和 \）
void mkdirs(char *path) {
    char *p = path;
    while (*p) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            mkdir(path);
            *p = '/';
        }
        p++;
    }
    mkdir(path);
}

// ---------- 多文件压缩 ----------
void compressMultipleFiles(int fileCount, char* fileList[], const char* outputFile) {
    FileList files;
    initFileList(&files);
    
    for (int i = 0; i < fileCount; i++) {
        struct stat st;
        if (stat(fileList[i], &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                collectFiles(fileList[i], &files, fileList[i]);
            } else {
                addFileToList(&files, fileList[i]);
            }
        } else {
            printf("Warning: cannot access %s\n", fileList[i]);
        }
    }
    
    if (files.count == 0) {
        printf("No files to compress.\n");
        freeFileList(&files);
        return;
    }
    
    // 压缩每个文件到内存
    for (int i = 0; i < files.count; i++) {
        const char* path = files.entries[i].path;
        FILE* f = fopen(path, "rb");
        if (!f) { printf("Cannot read file: %s, skip\n", path); continue; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        rewind(f);
        unsigned char* data = (unsigned char*)malloc(sz);
        fread(data, 1, sz, f);
        fclose(f);
        files.entries[i].data = data;
        files.entries[i].size = sz;
        
        long compSize;
        files.entries[i].compData = huffmanCompressData(data, sz, &compSize);
        files.entries[i].compSize = compSize;
        memcpy(files.entries[i].freqTable, freq, sizeof(freq));
    }
    
    FILE* fout = fopen(outputFile, "wb");
    if (!fout) { perror("Cannot create output file"); freeFileList(&files); return; }
    
    fwrite(MAGIC_MULTI, 1, 4, fout);
    int fileCountOut = files.count;
    fwrite(&fileCountOut, sizeof(int), 1, fout);
    long* offsets = (long*)malloc(sizeof(long) * files.count);
    fwrite(offsets, sizeof(long), files.count, fout); // 占位，稍后回填
    
    long* dataOffsets = (long*)malloc(sizeof(long) * files.count);
    for (int i = 0; i < files.count; i++) {
        dataOffsets[i] = ftell(fout);
        const char* relPath = files.entries[i].path;
        // 简化：使用完整路径作为存储名（实际可优化为相对路径）
        int nameLen = strlen(relPath);
        fwrite(&nameLen, sizeof(int), 1, fout);
        fwrite(relPath, 1, nameLen, fout);
        fwrite(&files.entries[i].size, sizeof(long), 1, fout);
        fwrite(&files.entries[i].compSize, sizeof(long), 1, fout);
        fwrite(files.entries[i].freqTable, sizeof(long), MAX_CHARS, fout);
        fwrite(files.entries[i].compData, 1, files.entries[i].compSize, fout);
    }
    
    // 回写偏移量
    fseek(fout, 4 + sizeof(int), SEEK_SET);
    fwrite(dataOffsets, sizeof(long), files.count, fout);
    
    fclose(fout);
    free(offsets);
    free(dataOffsets);
    freeFileList(&files);
    printf("Multiple-file compression done: %d files -> %s\n", fileCountOut, outputFile);
}

// ---------- 多文件解压 ----------
void decompressMultipleFiles(const char* inputFile, const char* outputDir) {
    FILE* fin = fopen(inputFile, "rb");
    if (!fin) { perror("Cannot open compressed file"); return; }
    char magic[5] = {0};
    fread(magic, 1, 4, fin);
    if (strcmp(magic, MAGIC_MULTI) != 0) {
        printf("Error: not a valid MUF multi-file archive\n");
        fclose(fin); return;
    }
    int fileCount;
    fread(&fileCount, sizeof(int), 1, fin);
    long* offsets = (long*)malloc(sizeof(long) * fileCount);
    fread(offsets, sizeof(long), fileCount, fin);
    
    mkdirs((char*)outputDir);
    
    for (int i = 0; i < fileCount; i++) {
        fseek(fin, offsets[i], SEEK_SET);
        int nameLen;
        fread(&nameLen, sizeof(int), 1, fin);
        char* filename = (char*)malloc(nameLen + 1);
        fread(filename, 1, nameLen, fin);
        filename[nameLen] = '\0';
        long origSize, compSize;
        fread(&origSize, sizeof(long), 1, fin);
        fread(&compSize, sizeof(long), 1, fin);
        long freqTable[MAX_CHARS];
        fread(freqTable, sizeof(long), MAX_CHARS, fin);
        unsigned char* compData = (unsigned char*)malloc(compSize);
        fread(compData, 1, compSize, fin);
        
        unsigned char* origData = huffmanDecompressData(compData, compSize, origSize, freqTable);
        free(compData);
        if (origData) {
            char outPath[1024];
            snprintf(outPath, sizeof(outPath), "%s/%s", outputDir, filename);
            // 统一路径分隔符
            for (char *c = outPath; *c; c++) if (*c == '\\') *c = '/';
            char parentPath[1024];
            strcpy(parentPath, outPath);
            char *lastSlash = strrchr(parentPath, '/');
            if (lastSlash) {
                *lastSlash = '\0';
                mkdirs(parentPath);
            }
            FILE* fout = fopen(outPath, "wb");
            if (fout) {
                fwrite(origData, 1, origSize, fout);
                fclose(fout);
                printf("Extracted: %s\n", outPath);
            } else {
                printf("Failed to create %s\n", outPath);
            }
            free(origData);
        }
        free(filename);
    }
    free(offsets);
    fclose(fin);
    printf("Multi-file decompression done.\n");
}

// ---------- 主函数 ----------
int main(int argc, char* argv[]) {
    if (argc < 4) {
        printf("Usage:\n");
        printf("  Single file compress: %s -c input output\n", argv[0]);
        printf("  Single file decompress: %s -d input output\n", argv[0]);
        printf("  Multi files/dir compress: %s -m output.muf file1 file2 dir1 ...\n", argv[0]);
        printf("  Multi files decompress: %s -u archive.muf output_dir\n", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "-c") == 0 && argc == 4) {
        compressSingleFile(argv[2], argv[3]);
    } else if (strcmp(argv[1], "-d") == 0 && argc == 4) {
        decompressSingleFile(argv[2], argv[3]);
    } else if (strcmp(argv[1], "-m") == 0 && argc >= 4) {
        compressMultipleFiles(argc-3, &argv[3], argv[2]);
    } else if (strcmp(argv[1], "-u") == 0 && argc == 4) {
        decompressMultipleFiles(argv[2], argv[3]);
    } else {
        printf("Invalid arguments. Use -c/-d for single, -m/-u for multi.\n");
        return 1;
    }
    return 0;
}
