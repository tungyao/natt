# GitHub Action: Windows GUI 编译

## 概述

创建一个GitHub Actions工作流，用于编译NATMesh Windows GUI客户端（natt-gui）。该工作流使用Conan管理依赖，通过CMake编译生成动态链接的Release版本可执行文件。

## 动机

| 需求 | 解决方案 |
|------|----------|
| 自动化编译 | GitHub Actions工作流，手动触发 |
| Windows兼容 | 使用windows-latest运行器 |
| 依赖管理 | Conan安装Qt 6.11.1和其他依赖 |
| 动态链接 | 生成natt-gui.exe和相关Qt DLL |
| 产物管理 | 上传到GitHub Artifact |

## 架构

```
.github/workflows/build-windows-gui.yml
├── 触发: workflow_dispatch
├── 运行环境: windows-latest
├── 步骤:
│   ├── 1. 检出代码
│   ├── 2. 安装Python 3.x
│   ├── 3. 安装Conan
│   ├── 4. 安装Qt 6.11.1依赖
│   ├── 5. 配置CMake
│   ├── 6. 编译natt-gui
│   └── 7. 上传Artifact
└── 产物: natt-gui.exe + Qt DLLs
```

## 工作流详细设计

### 触发条件

```yaml
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
```

### 运行环境

- 操作系统: `windows-latest`（Windows Server 2022）
- 编译器: MSVC（Windows默认）
- 架构: x64

### 工作步骤

#### 1. 检出代码

```yaml
- name: Checkout code
  uses: actions/checkout@v4
  with:
    fetch-depth: 0
```

#### 2. 安装Python

```yaml
- name: Set up Python
  uses: actions/setup-python@v5
  with:
    python-version: '3.11'
```

#### 3. 安装Conan

```yaml
- name: Install Conan
  run: |
    pip install conan
    conan profile detect --force
```

#### 4. 安装Qt 6.11.1依赖

```yaml
- name: Install Conan dependencies
  run: |
    conan install . -of=build --build=missing -s compiler.cppstd=23
```

#### 5. 配置CMake

```yaml
- name: Configure CMake
  run: |
    cmake -B build -DCMAKE_BUILD_TYPE=${{ github.event.inputs.build_type || 'Release' }} -DBUILD_GUI=ON
```

#### 6. 编译natt-gui

```yaml
- name: Build natt-gui
  run: |
    cmake --build build --target natt-gui --config ${{ github.event.inputs.build_type || 'Release' }}
```

#### 7. 上传Artifact

```yaml
- name: Upload natt-gui artifact
  uses: actions/upload-artifact@v4
  with:
    name: natt-gui-windows-x64
    path: |
      build/${{ github.event.inputs.build_type || 'Release' }}/natt-gui.exe
      build/generators/*.dll
    if-no-files-found: error
```

## 编译产物

### 主要产物

| 文件 | 说明 |
|------|------|
| `natt-gui.exe` | Windows GUI可执行文件（动态链接） |
| `Qt6Core.dll` | Qt核心库 |
| `Qt6Widgets.dll` | Qt Widgets库 |
| `Qt6Gui.dll` | Qt GUI库 |

### 依赖说明

- 动态链接需要Qt DLL在同一目录
- 使用Conan安装的Qt 6.11.1版本
- 编译类型默认为Release（可手动选择Debug/RelWithDebInfo）

## 错误处理

| 错误场景 | 处理方式 |
|----------|----------|
| Conan安装失败 | 工作流失败，显示错误日志 |
| CMake配置失败 | 工作流失败，显示CMake错误 |
| 编译失败 | 工作流失败，显示编译错误 |
| 无文件上传 | 工作流失败，显示"no-files-found" |

## 测试验证

### 本地测试

```bash
# 在Windows机器上测试
pip install conan
conan profile detect --force
conan install . -of=build --build=missing -s compiler.cppstd=23
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=ON
cmake --build build --target natt-gui --config Release
```

### GitHub Actions测试

1. 推送代码到仓库
2. 手动触发工作流
3. 检查编译日志
4. 下载Artifact验证natt-gui.exe

## 文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `.github/workflows/build-windows-gui.yml` | 新建 | GitHub Actions工作流 |

## 后续改进

| 改进项 | 优先级 | 说明 |
|--------|--------|------|
| 静态链接版本 | 中 | 生成单个exe文件 |
| 多架构支持 | 低 | 支持ARM64编译 |
| 自动发布Release | 中 | 编译成功后自动创建Release |
| 缓存Conan依赖 | 高 | 加速后续编译 |
