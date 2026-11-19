# BARR-C:2018 独立逐条审查记录

审查者：独立 Codex QA Agent（非本轮代码实现者）。审查日期：2026-11-06。

源版本：HEAD `78b9dae79bf38903cb1ae5cd8e1e7829b2ddcba4` 加当前工作区中 `Config/Debug/core_debug.h` 的宏修正。自有C范围：40个生产`.c`、54个C-only头/配置模板，共94个文件；文件清单见本记录末尾。第三方实现、C++、测试与生成产物不计入本矩阵；保留项目拥有的厂商配置和移植层。逐文件内容已由独立 Agent 阅读复核，辅助定位搜索不替代逐条判定；本记录不代表具名人类签字。审查仅包含源代码，未操作硬件。

官方规则索引：[Barr Group BARR-C:2018 PDF](https://barrgroup.com/sites/default/files/barr_c_coding_standard_2018.pdf)，出版页8–56（PDF页19–67）。此记录只列规则ID、对本工程的判定与证据，不复制标准正文。按照书中顶层字母条款和罗马子条款分别计数，逐项覆盖167项；统计脚本同时检查ID唯一性、缺项与总数。采用技能列明的2018勘误，特别是线程同步不能仅靠volatile。

范围内容指纹（路径+NUL+文件内容，按清单顺序）：`f163de6540d06108bceac25d05f1482d3000d752bcbad26dfe895d6c303c2c94`。后续任何C/头文件变更需补审；构建与测试结果由另一份独立软件验收记录提供，本矩阵不复用作者的测试结果。

**判定：逐条审查完成；严格全量合规 FAIL。** 未发现本次审查能确认的新增P0/P1功能回归，但存在大量既有规范偏差。完成审查不等于批准偏离，机械门禁通过也不等于全量BARR-C合规。

统计：BLOCKED=11, FAIL=57, N/A=10, PASS=80, PROJECT_OVERRIDE=9；合计167。

判定说明：`PASS`表示在上述源码范围内人工未发现该项违例；`PROJECT_OVERRIDE`仅引用现有明确项目规则（例如C11、120列、Attach及相应排版），不新增豁免；`N/A`表示本范围无适用构造；`FAIL`是具体非符合；`BLOCKED`为确有ABI/流程理由但尚缺具名批准记录或流程证据。BLOCKED不计入PASS，也不由Agent代签。

## 需要处理的组

- **控制流与维护安全**：1.3.a、1.4.a/b、8.1.a、8.2.b/d、8.4.a/b/d，重点证据是usb_debug_link与USB端点循环。本次没有证据表明这些既有写法已导致运行错误；不能将规范问题夸大为已发生故障。
- **类型、命名与文件契约**：4.2.a、4.3.b/c、5.1.a/b/c、5.6.a/b、6.1.d、7.1.*。应逐模块收口，或由人类负责人明确批准有边界的项目偏离；禁止为获得PASS而批量重命名厂商ABI。
- **文档与流程**：缩写表、模块/函数注释、模板、转换范围说明及ABI偏离批准。父Agent获用户授权的1–4修复与这些遗留规范问题应分开验收。
- **硬件与仿真**：本审查没有证明外设行为。按用户选择，HC32板级仿真是`BLOCKED_BY_MODEL`；HIL是`NOT_RUN`。

## 167项矩阵

| Rule ID | 判定 | 本工程证据/处置 |
|---|---|---|
| 1.1.a | PROJECT_OVERRIDE | cmake/CompilerOptions.cmake:32：显式C11；AGENTS.md与既有基线已选定。 |
| 1.1.b | PASS | CMake以文件语言分别调用C/C++编译器，C源码未作为C++编译。 |
| 1.1.c | PASS | 扩展集中于Platform的寄存器/中断/链接段桥接及Boot固定记录；未用于可移植core。 |
| 1.1.d | PASS | 逐个检查自有宏；未发现关键字替换。 |
| 1.2.a | FAIL | 项目覆盖为120列；bsp_status_led.h:36现有注释121列，尚未修正。 |
| 1.3.a | FAIL | storage.c:11、Boot/Core/Src/main.c:24等单语句if无复合体；项目Attach不豁免括号存在性。 |
| 1.3.b | PROJECT_OVERRIDE | .clang-format的BreakBeforeBraces:Attach显式覆盖。 |
| 1.4.a | FAIL | protocol.c:13、bsp_status_led.c:25等同时使用多种运算符，未完整显式分组。 |
| 1.4.b | FAIL | storage.c:6、usb_fw_update.c:289等逻辑操作数未分别加括号。 |
| 1.5.a | PASS | 对外命名多为协议/外设常见缩写；本次未发现妨碍解释的专有缩写。 |
| 1.5.b | FAIL | README/docs未找到版本管理的项目缩写表；存在PG/ACOK/ICG等项目术语。 |
| 1.6.a | FAIL | protocol.c:10–26大量转换无逐处范围注释；范围通常可推断，但不满足该记录要求。 |
| 1.7.a | PASS | 自有C词法检查未发现auto关键字用作存储类。 |
| 1.7.b | PASS | 自有C词法检查未发现register存储类。 |
| 1.7.c | PASS | bsp_clock.c与image_control_mcuboot.c共10处goto仅向前跳至out清理；偏好避免并非无条件禁用。 |
| 1.7.d | PASS | protocol.c:187与USB端点遍历的continue用于跳过已处理输入/无事件端点，人工复核了递增路径。 |
| 1.8.a | PASS | 内部驱动/协议辅助函数与状态大多static；公开符号由API、向量表、C运行时或调试读数使用。 |
| 1.8.b | FAIL | bsp_image_info.c:10地址在asm中变更属必要例外；manager.c:212的alignment等其他只读局部未const。 |
| 1.8.b.i | FAIL | manager.c:212、protocol.c:168等只读初始化局部仍可写。 |
| 1.8.b.ii | PASS | storage、protocol、manager的输入缓冲区/只读契约指针使用const。 |
| 1.8.b.iii | N/A | 自有结构体未直接定义MMIO只读位域；寄存器类型由SDK提供。 |
| 1.8.b.iv | FAIL | bsp_status_led.c:3–8数字常量均为宏，非预处理所需处仍未采用带类型常量。 |
| 1.8.c | PASS | USB回调标志、millis及MMIO使用volatile；多字段事件以临界区转交主循环。 |
| 1.8.c.i | PASS | usb_fw_update.c:204–214、usb_debug_link.c:16–17、bsp_timebase.c:8均显式volatile。 |
| 1.8.c.ii | N/A | 裸机无RTOS线程；按2018勘误不以volatile充当线程同步。 |
| 1.8.c.iii | PASS | MMIO经SDK的volatile寄存器类型访问；未发现自有非volatile寄存器映射。 |
| 1.8.c.iv | N/A | 自有延时使用SysTick/DWT寄存器差值，未用可优化掉的普通空计数延时。 |
| 2.1.a | PASS | 自有C允许单行注释；当前编译方言支持。 |
| 2.1.b | FAIL | easylogger/inc/elog_cfg.h:67起仍有以单行注释包裹的块注释起始符；厂商衍生配置需登记例外或整理。 |
| 2.1.c | FAIL | elog_cfg.h:68–83保留已注释的配置宏，未以显式预处理分支表达。 |
| 2.1.c.i | FAIL | 同elog_cfg.h:68–83；临时/示例禁用未区分并记录。 |
| 2.1.c.ii | PROJECT_OVERRIDE | Config/Debug/core_debug.h与CMake通过BSP_DEBUG控制日志，Release宏为空。 |
| 2.2.a | FAIL | boot_fault.h与寄存器相关头含字段片段注释，未全部写成完整句子。 |
| 2.2.b | FAIL | usb_debug_link.c:28–43相邻功能块间缺少空行；此处未由项目格式化器补齐。 |
| 2.2.c | PASS | 核心注释解释生命周期、保留缓存、读回验证及硬件约束；未发现大段逐语句复述。 |
| 2.2.d | PASS | 复杂升级/恢复路径有相应的生命周期与掉电顺序说明。 |
| 2.2.e | PASS | I2C bus-clear引用UM10204节号；UART引用SDK算法；MCUboot/FW协议语义可追到仓库文档。 |
| 2.2.f | N/A | 本范围没有必须依赖未入库图示才能解释的新增算法；已有架构/协议文档在docs。 |
| 2.2.g | FAIL | watchdog_scheduler的有符号deadline比较隐含小于半个tick周期的间隔约束，公共头未写明；板级3000ms满足但API条件不完整。 |
| 2.2.h | FAIL | bsp_clock_init、USB端点API等多数函数无逐函数Doxygen契约。 |
| 2.2.i | FAIL | 风险/原因注释普遍存在，但未一致采用指定大写标记；不把可读性判断冒充全量遵守。 |
| 2.2.i.i | FAIL | bsp_clock.c:74–75等未使用WARNING前缀。 |
| 2.2.i.ii | FAIL | bsp_i2c2.c:29–32的硬件恢复原因未使用NOTE前缀。 |
| 2.2.i.iii | N/A | 自有生产C中未发现待实现TODO任务。 |
| 3.1.a | PASS | 控制关键字和return后空格符合项目格式。 |
| 3.1.b | PASS | 赋值运算符空格符合格式；采用官方勘误纠正该条运算符列表。 |
| 3.1.c | PASS | 二元运算符空格符合项目格式。 |
| 3.1.d | PASS | 一元运算符与操作数之间无多余空格。 |
| 3.1.e | PROJECT_OVERRIDE | .clang-format的PointerAlignment:Left显式覆盖指针声明排版。 |
| 3.1.f | PASS | 三元表达式分隔符空格符合项目格式。 |
| 3.1.g | PASS | 成员访问符无环绕空格。 |
| 3.1.h | PASS | 数组下标无多余空格。 |
| 3.1.i | PASS | 括号内部空格符合SpacesInParentheses:false。 |
| 3.1.j | PROJECT_OVERRIDE | SpaceBeforeParens:ControlStatements指定函数声明/调用均紧贴括号。 |
| 3.1.k | PASS | 逗号后的空格符合项目格式。 |
| 3.1.l | PASS | 非无限循环for的分隔符空格符合项目格式。 |
| 3.1.m | PASS | 语句分号之前无多余空格。 |
| 3.2.a | PROJECT_OVERRIDE | AlignConsecutiveDeclarations:None显式选定不对齐连续变量声明。 |
| 3.2.b | PROJECT_OVERRIDE | 同AlignConsecutiveDeclarations:None，结构成员跟随项目格式。 |
| 3.2.c | PROJECT_OVERRIDE | AlignConsecutiveAssignments:None显式覆盖。 |
| 3.2.d | PASS | 自有预处理指令的#均位于列首。 |
| 3.3.a | PASS | 未发现同一行包含多个可执行语句；多变量声明另计8.1.a。 |
| 3.3.b | FAIL | usb_debug_link.c:28–43等自然函数块连续排列，缺少规定空行。 |
| 3.3.c | FAIL | storage.c:56等以右花括号结束，缺文件结束注释。 |
| 3.4.a | PASS | 项目自有C为4空格缩进。 |
| 3.4.b | PASS | case标签与各case内部缩进一致；最终break对齐差异另列8.3.a。 |
| 3.4.c | PASS | 长表达式按对齐/续行规则排版。 |
| 3.5.a | PASS | 94个审查文件字节检查未发现TAB。 |
| 3.6.a | PASS | 94个审查文件未发现CR字节。 |
| 3.6.b | PASS | 94个审查文件未发现规则禁止的控制字符。 |
| 4.1.a | PASS | 自有C模块文件名均为小写字母/数字/下划线；目录大小写不属于模块名。 |
| 4.1.b | BLOCKED | 40个生产C模块无不同前8字符冲突；但mcuboot_assert/config/logging等固定头共享mcuboot_前缀，完整头范围需ABI例外记录。 |
| 4.1.c | PASS | 公开头使用项目路径；未与ISO C/C++标准头同名。 |
| 4.1.d | PASS | 含C main入口的Boot模块文件名为main.c。 |
| 4.2.a | FAIL | boot_info.c、elog_port.c、fal_flash_hc32.c、config_checks.c、keys.c、syscalls.c、usb_dc_hc32f460.c无同名.h。 |
| 4.2.b | PASS | 审查范围的C头/配置模板均有预处理guard；同名main.h在不同映像，未跨包含。 |
| 4.2.c | FAIL | usb_fw_update_activity_from_manager_state仅供内部/测试使用却公开于usb_fw_update.h，头契约可更窄。 |
| 4.2.c.i | FAIL | boot_fault.h:38和fal_cfg.h:6存在extern变量声明；应记录调试/厂商ABI理由。 |
| 4.2.c.ii | PASS | 自有C头未定义可变对象存储；常量宏与static inline函数不分配对象存储。 |
| 4.2.d | PASS | 公开便携契约不包含平台私有头；平台头包含SDK属于已明确的底层边界。 |
| 4.3.a | PASS | 生产C基本按总线、Flash、协议、管理器和适配器职责划分。 |
| 4.3.b | FAIL | protocol.c:9起私有函数实现在公共函数之前，未遵循标准的章节顺序。 |
| 4.3.c | FAIL | Boot/Core/Src/main.c未包含main.h；另7个无同名头的模块见4.2.a。 |
| 4.3.d | PASS | 没有源代码include绝对路径。 |
| 4.3.e | FAIL | bsp_critical.h:4引入stdbool.h但头内未用bool；bsp_timebase.c:3的limits.h未用。 |
| 4.3.f | PASS | 未发现自有生产C包含.c文件。 |
| 4.4.a | FAIL | 仓库未维护统一C/.h模板；技能模板不等于本项目维护的模板。 |
| 5.1.a | FAIL | fw_update/error.h:5、usb_fw_update.h:10等新类型tag不以_t结尾。 |
| 5.1.b | FAIL | 大量struct/enum使用命名tag而非typedef；示例同5.1.a。 |
| 5.1.c | FAIL | bsp_external_watchdog.h:9的watchdog_gpio_write_t与watchdog_scheduler_t缺所在模块前缀。 |
| 5.2.a | PASS | 地址、长度、线协议、计时、寄存器值使用定宽整数；int用于返回码/SDK ABI。 |
| 5.2.b | BLOCKED | fal_flash_hc32.c三处long有barr-c:allow及FAL ABI理由；尚无具名批准者记录，不能自动批准。 |
| 5.2.c | PASS | char用于序列号、日志、字符串/字符；二进制数据使用uint8_t。 |
| 5.3.a | N/A | 自有类型未声明有符号位域；SDK的无符号位域属于外部ABI。 |
| 5.3.b | PASS | 复核CRC、掩码与移位：原始数据为无符号定宽类型，无对负数进行位操作的路径；小整数提升范围在目标32位int内。 |
| 5.3.c | PASS | 宽度敏感算术与比较使用无符号定宽量；错误码单独作有符号比较，显式转换处理ABI边界。 |
| 5.4.a | PASS | 仅I2C SDK初始化的float32_t误差输出；业务/协议未引入浮点运算。 |
| 5.4.b | PASS | 浮点限于SDK输出参数，不存在自有计算依赖浮点精度。 |
| 5.4.b.i | PASS | bsp_i2c2.c:128沿用SDK的float32_t；按2018勘误认可既有CMSIS类型。 |
| 5.4.b.ii | N/A | 自有生产C没有单精度浮点字面量。 |
| 5.4.b.iii | N/A | 自有逻辑无double精度计算依赖。 |
| 5.4.b.iv | PASS | 未发现自有浮点相等/不等比较。 |
| 5.4.b.v | N/A | SDK的误差输出没有被计算或用于决策，不存在需要isfinite校验的自有计算结果。 |
| 5.5.a | PASS | 线协议/FlashDB记录按字节序显式编码；Boot记录/保留请求由链接器固定地址与SIZEOF断言约束。 |
| 5.5.b | PASS | 自有协议不使用位域；HC32寄存器位域限定于SDK与目标GCC ABI，PLL改写已对照SDK定义。 |
| 5.6.a | FAIL | usb_debug_link.c:16、manager.h:113起多项内部布尔状态使用uint8_t。 |
| 5.6.b | FAIL | protocol.c:76对返回int的flags_are_valid直接使用!，未先显式关系转换。 |
| 6.1.a | PASS | 自有函数未命名为C/C++关键字。 |
| 6.1.b | PASS | 未覆盖ISO C标准库函数；syscalls桥接名属于newlib ABI，不是ISO函数重定义。 |
| 6.1.c | BLOCKED | syscalls.c的_close等与bsp_panic.c的__assert_func使用必需ABI名；需具名偏离登记。 |
| 6.1.d | FAIL | usb_fw_update_activity_from_manager_state与多个fw_update_*公开名超过31字符。 |
| 6.1.e | BLOCKED | NMI_Handler/HardFault_Handler/SysTick_Handler保持CMSIS向量固定名；需要明确平台偏离记录。 |
| 6.1.f | PASS | 自有宏均大写；SDK宏不属于本范围。 |
| 6.1.g | PASS | 自有可控函数名采用下划线分词；固定ABI名见6.1.e。 |
| 6.1.h | PASS | 公开函数名基本表达操作或判断，未发现无意义的自有过程名。 |
| 6.1.i | BLOCKED | watchdog_scheduler_*、newlib/USB/MCUboot回调名与文件模块前缀不同；厂商ABI部分有必要性，但未形成统一批准记录。 |
| 6.2.a | PASS | 自有生产C各函数未超过100行；manager拆为多项状态处理，未以文件行数代替函数行数。 |
| 6.2.b | BLOCKED | 没有提交打印分页证据；不能证明函数在打印页顶开始，属于文档/工具流程项。 |
| 6.2.c | PASS | 短路错误返回用于显式失败路径；这是偏好条款，当前多出口改善可读性而非自动违规。 |
| 6.2.d | BLOCKED | 正常公共API有头原型；syscalls.c未引入专属ABI声明头，无法证明每项newlib桥接都由编译器校验原型。 |
| 6.2.e | PASS | 私有辅助函数均static；USB回调、异常入口及调试符号按所需链接可见性公开。 |
| 6.2.f | PASS | 参数具名；有意不用的SDK回调参数以(void)明确处理。 |
| 6.3.a | BLOCKED | 日志/厂商配置/静态初始化校验保留参数宏；宏必要性可解释但尚无集中偏离记录。 |
| 6.3.b | FAIL | 日志及do-while空hook不满足该条每项表达式宏约束；必须分别登记语句宏/接口宏例外。 |
| 6.3.b.i | FAIL | core_debug.h:19日志宏与mcuboot_config.h:17空hook使用调用/语句体，未以整个表达式括号包裹。 |
| 6.3.b.ii | BLOCKED | 可控参数宏BSP_BOOT_INFO_CHECK/FLASH_AREA_IMAGE_*对参数有括号；__VA_ARGS__日志转发是厂商宏契约例外。 |
| 6.3.b.iii | PASS | 自有可控宏未重复求值同一实参；BSP_BOOT_INFO_CHECK各实参只出现一次。 |
| 6.3.b.iv | PASS | 自有参数宏未包含return/goto等跨调用者控制转移。 |
| 6.4.a | N/A | 裸机工程无RTOS线程/进程入口。 |
| 6.5.a | PASS | Cortex-M向量入口与SDK回调按目标ABI安装；普通Handler无需IAR式interrupt关键字，HardFault裸函数明确汇编桥接。 |
| 6.5.b | BLOCKED | 固定CMSIS Handler名不可改；USB的static usb_irq_handler为可控私有回调，尚未按_isr命名。 |
| 6.5.c | BLOCKED | USB ISR为static、SysTick在模块尾部；boot_fault.h暴露异常函数原型，虽无普通调用者，仍缺明确防误调用或ABI偏离策略。 |
| 6.5.d | PASS | startup_hc32f460.S为未处理异常及IRQ安装Default_Handler弱别名。 |
| 7.1.a | FAIL | usb_dc_hc32f460.c:214等变量名interrupt属于BARR明列的常见扩展保留名称。 |
| 7.1.b | PASS | errno使用为newlib要求，未声明同名自有变量遮蔽标准库对象。 |
| 7.1.c | PASS | 自有变量没有以下划线开头；系统/链接符号属于ABI，另列BLOCKED。 |
| 7.1.d | PASS | 审查范围自有变量名未发现超过31字符；宏名不属于变量。 |
| 7.1.e | FAIL | boot_handover.c:60与bsp_i2c2.c:45等循环变量i不足3字符；USB参数ep亦不足。 |
| 7.1.f | FAIL | bsp_clock.c:14–15局部stcXtalInit/stcMpllInit含大写。 |
| 7.1.g | PASS | 未发现把可变化数组规模编码进自有变量名；外设编号及协议字段编号用于身份而非可变规模。 |
| 7.1.h | FAIL | bsp_clock.c:14等局部变量使用camelCase，未以下划线分词。 |
| 7.1.i | FAIL | USB端点代码ep/i及多个协议读写辅助的短参数名不足以单独表达用途。 |
| 7.1.j | FAIL | usb_debug_link.c:16起与manager.c:17静态全局未统一g前缀；文件静态对象仍属全局存储。 |
| 7.1.k | FAIL | storage.c:28的storage/data以及多数其他指针参数不以p开头。 |
| 7.1.l | FAIL | flash_map_backend.c:37的area是双重指针，未使用pp前缀。 |
| 7.1.m | FAIL | usb_debug_link.c:16布尔信息configured/rx_ready等整数标志无b前缀。 |
| 7.1.n | FAIL | syscalls.c:7等文件句柄参数file无h前缀，ABI类型要求不要求该参数名。 |
| 7.1.o | FAIL | usb_debug_link.c:21全局指针service未按组合前缀命名。 |
| 7.2.a | PASS | 复核读取前赋值：协议局部、SDK输出、回调事件快照与static零初始化路径；无确认的未初始化读取。 |
| 7.2.b | PASS | 多数局部在使用附近声明；少量顶部声明对应多阶段错误处理，属于偏好条款。 |
| 7.2.c | FAIL | usb_debug_link.c:38在函数体之后继续声明interface/out_ep/in_ep等文件全局。 |
| 7.2.d | FAIL | usb_dc_hc32f460.c:182的ep在声明时无初始地址且未显式NULL，虽在使用前赋值。 |
| 8.1.a | FAIL | usb_debug_link.c:16–20、111–112多个变量共享同一声明。 |
| 8.2.a | PASS | 偏好短分支先行；错误早退整体遵循，未将所有合法长主分支判为违规。 |
| 8.2.b | FAIL | usb_debug_link.c:130–145多级收包/尾包状态分支超过两层。 |
| 8.2.c | PASS | 自有C条件中未发现赋值表达式；故意调用副作用函数不等于赋值。 |
| 8.2.d | FAIL | Boot/Core/Src/main.c:65–68的else-if链无末尾else；usb_event等亦有同类。 |
| 8.3.a | PROJECT_OVERRIDE | 项目clang-format将break与case内部语句对齐；按显式格式规则覆盖。 |
| 8.3.b | PASS | 自有C各switch存在default；多个case共用处理体不属于遗漏。 |
| 8.3.c | PASS | 没有确认的执行语句后意外贯穿；连续空case是显式分组。 |
| 8.4.a | FAIL | bsp_i2c2.c:45的9次总线清除、protocol.c:49的8位循环使用字面量边界。 |
| 8.4.b | FAIL | usb_dc_hc32f460.c:210/235的for更新子句同时修改epnum与pending。 |
| 8.4.c | FAIL | boot_fault.c:8与71的死循环使用while(1)。 |
| 8.4.d | FAIL | bsp_panic.c:19与syscalls.c:56的空for死循环体没有说明注释。 |
| 8.5.a | PASS | 10个goto均已按1.7.c复核为向前清理路径。 |
| 8.5.b | PASS | 未发现abort/exit/setjmp/longjmp调用；定义newlib _exit桥接不等于调用exit。 |
| 8.6.a | FAIL | storage.c:11等将NULL置于变量右侧；条件赋值告警不替代这一独立写法规则。 |

## 审查文件清单

- `App/Core/Inc/app_confirm.h`
- `App/Core/Inc/main.h`
- `App/Core/Inc/usb_debug_link.h`
- `App/Core/Inc/usb_fw_update.h`
- `App/Core/Src/app_confirm.c`
- `App/Core/Src/usb_debug_link.c`
- `App/Core/Src/usb_fw_update.c`
- `Boot/Core/Inc/main.h`
- `Boot/Core/Src/boot_info.c`
- `Boot/Core/Src/main.c`
- `Config/Application/usb_config.h`
- `Config/Debug/core_debug.h`
- `Config/Memory/boot_memory_map.h.in`
- `Config/Product/product_identity.h.in`
- `Platform/HC32F460/Config/bsp_board_config.h`
- `Platform/HC32F460/Config/hc32f4xx_conf.h`
- `Platform/HC32F460/Config/usb_app_conf.h`
- `Platform/HC32F460/Config/usb_bsp.h`
- `Platform/HC32F460/Inc/boot_fault.h`
- `Platform/HC32F460/Inc/boot_handover.h`
- `Platform/HC32F460/Inc/bsp_charger_signals.h`
- `Platform/HC32F460/Inc/bsp_clock.h`
- `Platform/HC32F460/Inc/bsp_compiler.h`
- `Platform/HC32F460/Inc/bsp_critical.h`
- `Platform/HC32F460/Inc/bsp_external_watchdog.h`
- `Platform/HC32F460/Inc/bsp_flash.h`
- `Platform/HC32F460/Inc/bsp_i2c2.h`
- `Platform/HC32F460/Inc/bsp_image_info.h`
- `Platform/HC32F460/Inc/bsp_isr.h`
- `Platform/HC32F460/Inc/bsp_log_uart.h`
- `Platform/HC32F460/Inc/bsp_panic.h`
- `Platform/HC32F460/Inc/bsp_recovery_request.h`
- `Platform/HC32F460/Inc/bsp_reset.h`
- `Platform/HC32F460/Inc/bsp_status_led.h`
- `Platform/HC32F460/Inc/bsp_timebase.h`
- `Platform/HC32F460/Inc/bsp_trng.h`
- `Platform/HC32F460/Inc/bsp_uart_brr.h`
- `Platform/HC32F460/Inc/bsp_usb.h`
- `Platform/HC32F460/Inc/bsp_write_protection.h`
- `Platform/HC32F460/Ports/easylogger/inc/elog_cfg.h`
- `Platform/HC32F460/Ports/easylogger/port/elog_port.c`
- `Platform/HC32F460/Ports/flashdb/Inc/fal_cfg.h`
- `Platform/HC32F460/Ports/flashdb/Inc/fdb_cfg.h`
- `Platform/HC32F460/Ports/flashdb/Src/fal_flash_hc32.c`
- `Platform/HC32F460/Ports/mcuboot/Inc/flash_map_backend/flash_map_backend.h`
- `Platform/HC32F460/Ports/mcuboot/Inc/mcuboot_config/mcuboot_assert.h`
- `Platform/HC32F460/Ports/mcuboot/Inc/mcuboot_config/mcuboot_config.h`
- `Platform/HC32F460/Ports/mcuboot/Inc/mcuboot_config/mcuboot_logging.h`
- `Platform/HC32F460/Ports/mcuboot/Inc/os/os_malloc.h`
- `Platform/HC32F460/Ports/mcuboot/Inc/sysflash/sysflash.h`
- `Platform/HC32F460/Ports/mcuboot/Src/config_checks.c`
- `Platform/HC32F460/Ports/mcuboot/Src/flash_map_backend.c`
- `Platform/HC32F460/Ports/mcuboot/Src/keys.c`
- `Platform/HC32F460/Src/boot_fault.c`
- `Platform/HC32F460/Src/boot_handover.c`
- `Platform/HC32F460/Src/bsp_charger_signals.c`
- `Platform/HC32F460/Src/bsp_clock.c`
- `Platform/HC32F460/Src/bsp_external_watchdog.c`
- `Platform/HC32F460/Src/bsp_flash.c`
- `Platform/HC32F460/Src/bsp_i2c2.c`
- `Platform/HC32F460/Src/bsp_image_info.c`
- `Platform/HC32F460/Src/bsp_isr.c`
- `Platform/HC32F460/Src/bsp_log_uart.c`
- `Platform/HC32F460/Src/bsp_panic.c`
- `Platform/HC32F460/Src/bsp_recovery_request.c`
- `Platform/HC32F460/Src/bsp_reset.c`
- `Platform/HC32F460/Src/bsp_status_led.c`
- `Platform/HC32F460/Src/bsp_timebase.c`
- `Platform/HC32F460/Src/bsp_trng.c`
- `Platform/HC32F460/Src/bsp_usb.c`
- `Platform/HC32F460/Src/bsp_write_protection.c`
- `Platform/HC32F460/Src/syscalls.c`
- `Platform/HC32F460/Src/usb_dc_hc32f460.c`
- `components/fw_update/backends/flashdb/include/fw_update/flashdb_product_config.h`
- `components/fw_update/backends/flashdb/src/flashdb_product_config.c`
- `components/fw_update/backends/mcuboot/include/fw_update/admin_ecdsa_auth.h`
- `components/fw_update/backends/mcuboot/include/fw_update/image_control_mcuboot.h`
- `components/fw_update/backends/mcuboot/include/fw_update/image_storage_mcuboot.h`
- `components/fw_update/backends/mcuboot/src/admin_ecdsa_auth.c`
- `components/fw_update/backends/mcuboot/src/image_control_mcuboot.c`
- `components/fw_update/backends/mcuboot/src/image_storage_mcuboot.c`
- `components/fw_update/include/fw_update/admin_auth.h`
- `components/fw_update/include/fw_update/boot_control.h`
- `components/fw_update/include/fw_update/error.h`
- `components/fw_update/include/fw_update/manager.h`
- `components/fw_update/include/fw_update/product_config.h`
- `components/fw_update/include/fw_update/protocol.h`
- `components/fw_update/include/fw_update/storage.h`
- `components/fw_update/src/admin_auth.c`
- `components/fw_update/src/boot_control.c`
- `components/fw_update/src/manager.c`
- `components/fw_update/src/product_config.c`
- `components/fw_update/src/protocol.c`
- `components/fw_update/src/storage.c`
