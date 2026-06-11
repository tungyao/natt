# GitHub Action: Windows GUI 编译实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 创建一个GitHub Actions工作流，用于编译NATMesh Windows GUI客户端（natt-gui），使用Conan管理依赖，通过CMake编译生成动态链接的Release版本可执行文件。

**Architecture:** 使用GitHub Actions的workflow_dispatch触发器，手动触发工作流。在windows-latest运行器上安装Python、Conan、Qt 6.11.1依赖，然后使用CMake配置和编译natt-gui，最后上传编译产物到GitHub Artifact。

**Tech Stack:** GitHub Actions, Python, Conan, CMake, Qt 6.11.1, MSVC

---

### Task 1: 创建GitHub Actions工作流目录

**Files:**
- Create: `.github/workflows/`

- [ ] **Step 1: 创建目录结构**

```bash
mkdir -p .github/workflows
```

- [ ] **Step 2: 验证目录创建**

```bash
ls -la .github/workflows
```

Expected: 目录存在且为空

- [ ] **Step 3: 提交**

```bash
git add .github/
git commit -m "chore: create GitHub Actions workflows directory"
```

### Task 2: 创建工作流文件

**Files:**
- Create: `.github/workflows/build-windows-gui.yml`

- [ ] **Step 1: 创建工作流文件**

```yaml
name: Build Windows GUI

on:
  workflow_dispatch:
    inputs:
      build_type:
        description: '编译类型'
        required: false
        default: 'Release'
        type: choice
        options:
          - Release
          - Debug
          - RelWithDebInfo

jobs:
  build:
    runs-on: windows-latest
    
    steps:
      - name: Checkout code
        uses: actions/checkout@v4
        with:
          fetch-depth: 0
      
      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      
      - name: Install Conan
        run: |
          pip install conan
          conan profile detect --force
      
      - name: Install Conan dependencies
        run: |
          conan install . -of=build --build=missing -s compiler.cppstd=23
      
      - name: Configure CMake
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=${{ github.event.inputs.build_type || 'Release' }} -DBUILD_GUI=ON
      
      - name: Build natt-gui
        run: |
          cmake --build build --target natt-gui --config ${{ github.event.inputs.build_type || 'Release' }}
      
      - name: Upload natt-gui artifact
        uses: actions/upload-artifact@v4
        with:
          name: natt-gui-windows-x64
          path: |
            build/${{ github.event.inputs.build_type || 'Release' }}/natt-gui.exe
            build/generators/*.dll
          if-no-files-found: error
```

- [ ] **Step 2: 验证文件内容**

```bash
cat .github/workflows/build-windows-gui.yml
```

Expected: 显示完整的YAML内容

- [ ] **Step 3: 提交**

```bash
git add .github/workflows/build-windows-gui.yml
git commit -m "feat: add GitHub Actions workflow for Windows GUI build"
```

### Task 3: 验证工作流语法

**Files:**
- None (验证步骤)

- [ ] **Step 1: 安装yamllint**

```bash
pip install yamllint
```

- [ ] **Step 2: 验证YAML语法**

```bash
yamllint .github/workflows/build-windows-gui.yml
```

Expected: 无错误输出

- [ ] **Step 3: 提交验证结果**

```bash
git add .
git commit -m "chore: validate GitHub Actions workflow syntax"
```

### Task 4: 测试工作流（可选）

**Files:**
- None (测试步骤)

- [ ] **Step 1: 推送代码到GitHub**

```bash
git push origin main
```

- [ ] **Step 2: 手动触发工作流**

1. 访问GitHub仓库
2. 点击"Actions"标签
3. 选择"Build Windows GUI"工作流
4. 点击"Run workflow"
5. 选择"Release"编译类型
6. 点击"Run workflow"

- [ ] **Step 3: 监控工作流执行**

1. 等待工作流完成
2. 检查每个步骤的执行结果
3. 如果有错误，查看错误日志

- [ ] **Step 4: 下载编译产物**

1. 工作流完成后，点击"Actions"标签
2. 选择最新的工作流运行
3. 在"Artifacts"部分下载"natt-gui-windows-x64"
4. 解压验证natt-gui.exe和Qt DLL存在

## 错误处理

| 错误场景 | 处理方式 |
|----------|----------|
| YAML语法错误 | 使用yamllint检查并修复 |
| Conan安装失败 | 检查Python版本和pip权限 |
| CMake配置失败 | 检查Qt依赖是否正确安装 |
| 编译失败 | 检查MSVC编译器和C++23支持 |
| 无文件上传 | 检查编译产物路径是否正确 |

## 验证检查清单

- [ ] 工作流文件语法正确
- [ ] 手动触发功能正常
- [ ] 编译过程无错误
- [ ] 产物包含natt-gui.exe
- [ ] 产物包含Qt DLL文件
- [ ] 产物可下载

## 后续改进

| 改进项 | 优先级 | 说明 |
|--------|--------|------|
| 添加Conan依赖缓存 | 高 | 加速后续编译 |
| 添加构建状态徽章 | 中 | 显示在README中 |
| 支持静态链接版本 | 中 | 生成单个exe文件 |
| 自动发布Release | 低 | 编译成功后自动创建Release |
