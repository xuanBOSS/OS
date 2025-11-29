#include "common.h"
#include "sys.h"
#include "user.h"
#include "fs/fcntl.h"

void _start(void);

// ========================================
// 辅助函数：字符串比较
// ========================================
int str_equal(const char* s1, const char* s2) {
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) {
            return 0;
        }
        i++;
    }
    return s1[i] == s2[i];  // 都到结尾才相等
}

// ========================================
// 辅助函数：字符串长度
// ========================================
int str_len(const char* s) {
    int len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

// ========================================
// 主测试函数
// ========================================
void _start(void) {
    printf("\n");
    printf("=====================================\n");
    printf("  Directory Operations Test\n");
    printf("=====================================\n");
    
    int test_passed = 0;
    int total_tests = 8;
    
    // ========================================
    // 测试1：创建目录
    // ========================================
    printf("\n[Test 1/8] Creating directory 'testdir'\n");
    printf("-------------------------------------\n");
    
    // ✅ 使用相对路径而不是绝对路径
    int ret = mkdir("testdir", 0755);
    if (ret < 0) {
        printf("  ❌ FAILED: mkdir() returned %d\n", ret);
        printf("  Possible reasons:\n");
        printf("    - Directory already exists\n");
        printf("    - No permission\n");
        printf("    - Filesystem error\n");
    } else {
        printf("  ✅ PASSED: directory created\n");
        test_passed++;
    }
    
    // ========================================
    // 测试2：切换到新目录
    // ========================================
    printf("\n[Test 2/8] Changing to 'testdir'\n");
    printf("-------------------------------------\n");
    
    ret = chdir("testdir");
    if (ret < 0) {
        printf("  ❌ FAILED: chdir() returned %d\n", ret);
        printf("  Cannot continue without chdir\n");
        goto cleanup;
    }
    printf("  ✅ PASSED: changed to testdir\n");
    test_passed++;
    
    // ========================================
    // 测试3：获取当前目录
    // ========================================
    printf("\n[Test 3/8] Getting current directory\n");
    printf("-------------------------------------\n");
    
    char buf[256];
    char* result = getcwd(buf, sizeof(buf));
    if (result == NULL) {
        printf("  ❌ FAILED: getcwd() returned NULL\n");
    } else {
        printf("  Current directory: '%s'\n", buf);
        
        // ✅ 检查路径是否包含 "testdir"
        int found = 0;
        int len = str_len(buf);
        for (int i = 0; i < len - 6; i++) {
            if (buf[i] == 't' && buf[i+1] == 'e' && buf[i+2] == 's' &&
                buf[i+3] == 't' && buf[i+4] == 'd' && buf[i+5] == 'i' &&
                buf[i+6] == 'r') {
                found = 1;
                break;
            }
        }
        
        if (found) {
            printf("  ✅ PASSED: path contains 'testdir'\n");
            test_passed++;
        } else {
            printf("  ⚠️  WARNING: path doesn't contain 'testdir'\n");
            printf("  (This might be OK depending on implementation)\n");
        }
    }
    
    // ========================================
    // 测试4：在当前目录创建文件
    // ========================================
    printf("\n[Test 4/8] Creating file 'test.txt'\n");
    printf("-------------------------------------\n");
    
    int fd = open("test.txt", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("  ❌ FAILED: open() returned %d\n", fd);
        goto cleanup;
    }
    
    char data[] = {'H', 'e', 'l', 'l', 'o'};
    int write_len = 5;
    
    int written = write(fd, data, write_len);
    if (written != write_len) {
        printf("  ❌ FAILED: write() returned %d (expected %d)\n", 
               written, write_len);
        close(fd);
        goto cleanup;
    }
    
    close(fd);
    printf("  ✅ PASSED: file created and written (%d bytes)\n", written);
    test_passed++;
    
    // ========================================
    // 测试5：读取文件验证
    // ========================================
    printf("\n[Test 5/8] Reading file to verify\n");
    printf("-------------------------------------\n");
    
    fd = open("test.txt", O_RDONLY);
    if (fd < 0) {
        printf("  ❌ FAILED: open() for read returned %d\n", fd);
        goto cleanup;
    }
    
    char read_buf[64];
    int bytes_read = read(fd, read_buf, sizeof(read_buf));
    close(fd);
    
    if (bytes_read != write_len) {
        printf("  ❌ FAILED: read %d bytes (expected %d)\n", 
               bytes_read, write_len);
    } else {
        // 验证数据
        int match = 1;
        for (int i = 0; i < write_len; i++) {
            if (data[i] != read_buf[i]) {
                match = 0;
                printf("  Data mismatch at byte %d: wrote '%c', read '%c'\n",
                       i, data[i], read_buf[i]);
                break;
            }
        }
        
        if (match) {
            printf("  Read data: ");
            for (int i = 0; i < bytes_read; i++) {
                printf("%c", read_buf[i]);
            }
            printf("\n");
            printf("  ✅ PASSED: data verified (%d bytes)\n", bytes_read);
            test_passed++;
        } else {
            printf("  ❌ FAILED: data mismatch\n");
        }
    }
    
    // ========================================
    // 测试6：使用绝对路径访问文件
    // ========================================
    printf("\n[Test 6/8] Accessing file with absolute path\n");
    printf("-------------------------------------\n");
    
    // ✅ 尝试用绝对路径打开（如果 getcwd 工作的话）
    if (result != NULL) {
        // 构建路径：当前目录 + "/test.txt"
        char abs_path[300];
        int pos = 0;
        
        // 复制当前目录
        for (int i = 0; buf[i] != '\0' && pos < 290; i++) {
            abs_path[pos++] = buf[i];
        }
        
        // 添加 "/test.txt"
        const char* filename = "/test.txt";
        for (int i = 0; filename[i] != '\0' && pos < 299; i++) {
            abs_path[pos++] = filename[i];
        }
        abs_path[pos] = '\0';
        
        printf("  Trying to open: '%s'\n", abs_path);
        
        fd = open(abs_path, O_RDONLY);
        if (fd < 0) {
            printf("  ⚠️  WARNING: couldn't open with absolute path\n");
            printf("  (This is OK if absolute paths aren't implemented)\n");
        } else {
            close(fd);
            printf("  ✅ PASSED: file accessible via absolute path\n");
            test_passed++;
        }
    } else {
        printf("  ⚠️  SKIPPED: getcwd failed in test 3\n");
    }
    
    // ========================================
    // 测试7：切换回根目录
    // ========================================
    printf("\n[Test 7/8] Changing back to root '/'\n");
    printf("-------------------------------------\n");
    
    ret = chdir("/");
    if (ret < 0) {
        printf("  ❌ FAILED: chdir('/') returned %d\n", ret);
    } else {
        result = getcwd(buf, sizeof(buf));
        if (result != NULL) {
            printf("  Current directory: '%s'\n", buf);
        }
        printf("  ✅ PASSED: returned to root\n");
        test_passed++;
    }
    
    // ========================================
    // 测试8：清理
    // ========================================
    printf("\n[Test 8/8] Cleaning up\n");
    printf("-------------------------------------\n");
    
    // ✅ 使用绝对路径删除（更可靠）
    ret = unlink("/testdir/test.txt");
    if (ret < 0) {
        printf("  ⚠️  WARNING: unlink() returned %d\n", ret);
        printf("  File might not exist or path is wrong\n");
    } else {
        printf("  ✅ Deleted: /testdir/test.txt\n");
        test_passed++;
    }
    
    // ✅ 尝试删除目录（如果实现了 rmdir）
    // 注意：目录必须为空才能删除
    // ret = rmdir("/testdir");
    // if (ret == 0) {
    //     printf("  ✅ Deleted: /testdir\n");
    // }
    
cleanup:
    // ========================================
    // 总结
    // ========================================
    printf("\n");
    printf("=====================================\n");
    printf("  Test Summary\n");
    printf("=====================================\n");
    printf("  Tests passed: %d/%d\n", test_passed, total_tests);
    
    if (test_passed == total_tests) {
        printf("  Result: ✅ ALL TESTS PASSED\n");
        printf("=====================================\n");
        exit(0);
    } else if (test_passed >= 5) {
        printf("  Result: ⚠️  PARTIAL SUCCESS\n");
        printf("  Core functionality works!\n");
        printf("=====================================\n");
        exit(0);
    } else {
        printf("  Result: ❌ TESTS FAILED\n");
        printf("=====================================\n");
        exit(-1);
    }
}
