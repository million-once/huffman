#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHARS 256

// Huffman tree node
typedef struct Node {
    unsigned char ch;
    long freq;
    struct Node *left, *right;
} Node;

// Global frequency table
long freq[MAX_CHARS] = {0};

// Encoding table: code string for each character (e.g., "101")
char codes[MAX_CHARS][MAX_CHARS];
int code_len[MAX_CHARS];

// ---------- Build Huffman tree ----------
Node* createNode(unsigned char ch, long freq, Node* left, Node* right) {
    Node* node = (Node*)malloc(sizeof(Node));
    node->ch = ch;
    node->freq = freq;
    node->left = left;
    node->right = right;
    return node;
}

// Build Huffman tree from frequency table (simple linear scan, no heap)
Node* buildHuffmanTree() {
    Node* nodes[MAX_CHARS];
    int nodeCount = 0;
    
    // Create leaf nodes for characters with positive frequency
    for (int i = 0; i < MAX_CHARS; i++) {
        if (freq[i] > 0) {
            nodes[nodeCount++] = createNode((unsigned char)i, freq[i], NULL, NULL);
        }
    }
    
    // Special case: only one distinct character
    if (nodeCount == 1) {
        return nodes[0];
    }
    
    // Repeatedly merge two smallest nodes
    while (nodeCount > 1) {
        // Find two smallest
        int min1 = 0, min2 = 1;
        if (nodes[min1]->freq > nodes[min2]->freq) {
            int tmp = min1; min1 = min2; min2 = tmp;
        }
        for (int i = 2; i < nodeCount; i++) {
            if (nodes[i]->freq < nodes[min1]->freq) {
                min2 = min1;
                min1 = i;
            } else if (nodes[i]->freq < nodes[min2]->freq) {
                min2 = i;
            }
        }
        
        // Merge
        Node* new = createNode(0, nodes[min1]->freq + nodes[min2]->freq,
                               nodes[min1], nodes[min2]);
        
        // Remove two smallest and add new node
        nodes[min1] = new;
        nodes[min2] = nodes[nodeCount-1];
        nodeCount--;
    }
    return nodes[0];
}

// Generate encoding table (recursive)
void generateCodes(Node* node, char* buffer, int depth) {
    if (!node) return;
    if (!node->left && !node->right) {
        buffer[depth] = '\0';
        strcpy(codes[node->ch], buffer);
        code_len[node->ch] = depth;
        return;
    }
    if (node->left) {
        buffer[depth] = '0';
        generateCodes(node->left, buffer, depth+1);
    }
    if (node->right) {
        buffer[depth] = '1';
        generateCodes(node->right, buffer, depth+1);
    }
}

// Free tree memory
void freeTree(Node* node) {
    if (!node) return;
    freeTree(node->left);
    freeTree(node->right);
    free(node);
}

// ---------- Compression ----------
void compressFile(const char* inputFile, const char* outputFile) {
    FILE* fin = fopen(inputFile, "rb");
    if (!fin) { perror("Failed to open input file"); return; }
    
    // 1. Count frequencies
    memset(freq, 0, sizeof(freq));
    int ch;
    while ((ch = fgetc(fin)) != EOF) {
        freq[ch]++;
    }
    rewind(fin);
    
    // 2. Build tree and generate codes
    Node* root = buildHuffmanTree();
    char buf[MAX_CHARS] = {0};
    generateCodes(root, buf, 0);
    
    // 3. Write compressed file
    FILE* fout = fopen(outputFile, "wb");
    if (!fout) { perror("Failed to open output file"); fclose(fin); return; }
    
    // 3.1 Write header: magic "HUFF", then 256 longs (frequency table)
    fwrite("HUFF", 1, 4, fout);
    fwrite(freq, sizeof(long), MAX_CHARS, fout);
    
    // 3.2 Write original file size (to know when to stop during decompression)
    fseek(fin, 0, SEEK_END);
    long fileSize = ftell(fin);
    rewind(fin);
    fwrite(&fileSize, sizeof(long), 1, fout);
    
    // 3.3 Write bit-packed encoded data
    unsigned char bitBuffer = 0;
    int bitsInBuffer = 0;
    int c;
    while ((c = fgetc(fin)) != EOF) {
        char* code = codes[c];
        for (int i = 0; i < code_len[c]; i++) {
            if (code[i] == '1')
                bitBuffer |= (1 << (7 - bitsInBuffer));
            bitsInBuffer++;
            if (bitsInBuffer == 8) {
                fputc(bitBuffer, fout);
                bitBuffer = 0;
                bitsInBuffer = 0;
            }
        }
    }
    // Write last partial byte if any
    if (bitsInBuffer > 0) {
        fputc(bitBuffer, fout);
    }
    
    fclose(fin);
    fclose(fout);
    freeTree(root);
    printf("Compression done: %s -> %s\n", inputFile, outputFile);
}

// ---------- Decompression ----------
void decompressFile(const char* inputFile, const char* outputFile) {
    FILE* fin = fopen(inputFile, "rb");
    if (!fin) { perror("Failed to open compressed file"); return; }
    
    // 1. Read header and check magic
    char magic[5] = {0};
    fread(magic, 1, 4, fin);
    if (strcmp(magic, "HUFF") != 0) {
        printf("Error: not a valid Huffman compressed file\n");
        fclose(fin);
        return;
    }
    
    // 2. Read frequency table
    long freq2[MAX_CHARS];
    fread(freq2, sizeof(long), MAX_CHARS, fin);
    
    // 3. Read original file size
    long originalSize;
    fread(&originalSize, sizeof(long), 1, fin);
    
    // 4. Rebuild Huffman tree
    memcpy(freq, freq2, sizeof(freq));
    Node* root = buildHuffmanTree();
    
    // 5. Decompress
    FILE* fout = fopen(outputFile, "wb");
    if (!fout) { perror("Failed to create output file"); fclose(fin); return; }
    
    long written = 0;
    Node* cur = root;
    int byte;
    while (written < originalSize && (byte = fgetc(fin)) != EOF) {
        for (int bit = 7; bit >= 0; bit--) {
            if (written >= originalSize) break;
            int b = (byte >> bit) & 1;
            if (b == 0)
                cur = cur->left;
            else
                cur = cur->right;
            
            if (!cur->left && !cur->right) {
                fputc(cur->ch, fout);
                written++;
                cur = root;
            }
        }
    }
    
    fclose(fin);
    fclose(fout);
    freeTree(root);
    printf("Decompression done: %s -> %s\n", inputFile, outputFile);
}

// ---------- Main ----------
int main(int argc, char* argv[]) {
    if (argc != 4) {
        printf("Usage:\n");
        printf("  Compress: %s -c source_file compressed_file\n", argv[0]);
        printf("  Decompress: %s -d compressed_file output_file\n", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "-c") == 0) {
        compressFile(argv[2], argv[3]);
    } else if (strcmp(argv[1], "-d") == 0) {
        decompressFile(argv[2], argv[3]);
    } else {
        printf("Unknown option %s, please use -c or -d\n", argv[1]);
        return 1;
    }
    return 0;
}
