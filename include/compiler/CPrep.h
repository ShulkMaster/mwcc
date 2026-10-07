#ifndef COMPILER_CPREP_H
#define COMPILER_CPREP_H

#include "version.h"
#include "compiler/common.h"
#include "driver/Files.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
struct IROOptNode {
    struct IROOptNode *next;
    SInt32 code;
    UInt8 flag;
    UInt8 value;
};
#pragma pack(pop)
#pragma pack(push, 1)
struct BrowseOptions {
    SInt8 browseOption; /* 0x00: CPrepCU browseOption view of CPrep_GetDependencyState settings. */
    UInt8 browseEnums;  /* 0x01: CDecl tests this setting while declaring enums. */
    UInt8 browseMacros; /* 0x02: CPrepCU browseMacros view of CPrep_GetDependencyState settings. */
    UInt8 fe9;
    UInt8 pad_ea;
    UInt8 feb;
    UInt8 padec[0x0a];
};
#pragma pack(pop)
struct BufferedToken {
    short token;
    short value_kind;
    struct PFile *tokenfile;
    int tokenoffset;
    int tokenline;
    union {
        CInt64 integer; /* 0x10: CPrepTokenizer_GetNextToken saves/restores token_integer when token == -1 */
        struct {
            int first;
            int second;
        } words; /* 0x10: CPrepTokenizer_GetNextToken, token == -3 (name), -4/-5 (string length) */
        struct {
            char *data;
            int length;
        } string;       /* 0x10: CPrepTokenizer_GetNextToken, token == -4 or -5 */
        Float floating; /* 0x10: CPrepTokenizer_GetNextToken, token == -2, saves/restores token_float */
    } value;
};
#pragma options align = mac68k
struct CNameRef {
    SInt32 a;
    SInt32 b;
    SInt16 c;
};
#pragma options align = reset
#pragma options align = mac68k
struct COpts {
#if VERSION >= VERSION_GC_2_7
    UInt8 nativeByteOrder;                               /* 0x00: little_endian */
    UInt8 field_0x01;                                    /* 0x01 */
    SInt16 processor;                                    /* 0x02 */
    SInt8 processorModel;                                /* 0x04 */
    UInt8 instructionSchedulingMode;                     /* 0x05 */
    unsigned char cOptimizerDumpEnabled;                 /* 0x06: debug_listing */
    UInt8 fba;                                           /* 0x07: profile */
    char emitExtraAssemblyData;                          /* 0x08 */
    Boolean f08;                                         /* 0x09: disable_registers */
    char debugOptions;                                   /* 0x0a: fp_contract */
    UInt8 no_register_save_helpers;                      /* 0x0b: no_register_save_helpers */
    Boolean ppcUnrollSpeculative;                        /* 0x0c */
    UInt8 field_0x0d;                                    /* 0x0d */
    SInt16 ppcUnrollInstructionsLimit;                   /* 0x0e */
    SInt16 ppcUnrollFactorLimit;                         /* 0x10 */
    UInt8 altivecModel;                                  /* 0x12 */
    UInt8 altivecVrsave;                                 /* 0x13 */
    UInt8 codeAlignment;                                 /* 0x14 */
    char emitSerializedAssemblyFormat;                   /* 0x15: cats */
    char f14;                                            /* 0x16: force_cats */
    UInt8 field_0x17;                                    /* 0x17 */
    SInt32 smallDataLimit;                               /* 0x18 */
    int smallBSSLimit;                                   /* 0x1c */
    UInt8 reuseSectionSymbols;                           /* 0x20: pool_data */
    UInt8 f20;                                           /* 0x21 */
    char debugEnabled;                                   /* 0x22 */
    Boolean operandsDebug;                               /* 0x23 */
    UInt8 f25;                                           /* 0x24 */
    UInt8 f26;                                           /* 0x25 */
    char f27;                                            /* 0x26 */
    SInt8 rel109Offset;                                  /* 0x27 */
    struct ObjGenSection *textSection;                   /* 0x28 */
    struct ObjGenSection *dataSection;                   /* 0x2c */
    struct ObjGenSection *bssSection;                    /* 0x30 */
    struct ObjGenSection *smallDataSection;              /* 0x34 */
    struct ObjGenSection *smallBSSSection;               /* 0x38 */
    SInt32 sectionHeaderTableSize;                       /* 0x3c */
    struct SectionSymbolAttributes **sectionHeaderTable; /* 0x40 */
    struct InterruptList *interruptList;                 /* 0x44 */
    struct InterruptGenerationRecord *interruptOptions;  /* 0x48 */
    UInt8 f4e;                                           /* 0x4c: incompatible_return_small_structs */
    Boolean emitMainFileObject;                          /* 0x4d: create_file_object */
    UInt8 returnStructsInMemory;                         /* 0x4e: incompatible_sfpe_double_params */
    UInt8 ibm_stackframe;                                /* 0x4f: ibm_stackframe */
    UInt8 field_0x50;                                    /* 0x50 */
    UInt8 field_0x51;                                    /* 0x51 */
    UInt8 gecko_float_typecons;                          /* 0x52: gecko_float_typecons */
    UInt8 disable_vector_frame;                          /* 0x53: disable_vector_frame */
    UInt8 ppc_opt_bclr_bcctr;                            /* 0x54: ppc_opt_bclr_bcctr */
    UInt8 useRegisterSaveHelpers;                        /* 0x55: use_lmw_stmw */
    UInt8 field_0x56;                                    /* 0x56 */
    UInt8 misaligned_mem_access;                         /* 0x57: misaligned_mem_access */
    UInt8 switch_tables;                                 /* 0x58: switch_tables */
    UInt8 prepare_compress;                              /* 0x59: prepare_compress */
    UInt8 field_0x5a;                                    /* 0x5a */
    UInt8 unsafe_global_reg_vars;                        /* 0x5b: unsafe_global_reg_vars */
    UInt8 ignore_global_reg_vars;                        /* 0x5c: ignore_global_reg_vars */
    UInt8 field_0x5d;                                    /* 0x5d */
    UInt32 field_0x5e;                                   /* 0x5e */
    UInt8 gprfloatcopy;                                  /* 0x62: gprfloatcopy */
    UInt8 old_style_bitfields;                           /* 0x63: old_style_bitfields */
    UInt8 asmsemicolcomment;                             /* 0x64: asmsemicolcomment */
    UInt8 asmpoundcomment;                               /* 0x65: asmpoundcomment */
    UInt8 field_0x66;                                    /* 0x66 */
    UInt8 field_0x67;                                    /* 0x67 */
    char *f54;                                           /* 0x68 */
    SInt16 inlineLimit;                                  /* 0x6c */
    SInt32 inlineMaxSize;                                /* 0x6e */
    SInt32 inlineMaxTotalSize;                           /* 0x72 */
    UInt8 inline_bottom_up;                              /* 0x76: inline_bottom_up */
    UInt8 cplusplus;                                     /* 0x77: cplusplus */
    UInt8 f5b;                                           /* 0x78: ecplusplus */
    UInt8 f5c;                                           /* 0x79: objective_c */
    SInt8 f5d;                                           /* 0x7a: objc_strict */
    char f5e;                                            /* 0x7b: ARM_conform */
    char f5f;                                            /* 0x7c: ARM_scoping */
    Boolean f60;                                         /* 0x7d: require_prototypes */
    char trigraphs;                                      /* 0x7e: trigraphs */
    UInt8 f62;                                           /* 0x7f: only_std_keywords */
    UInt8 f63;                                           /* 0x80: enumsalwaysint */
    UInt8 rejectZeroLengthArrayMembers;                  /* 0x81: ANSI_strict */
    UInt8 f65;                                           /* 0x82: mpwc_relax */
    UInt8 f66;                                           /* 0x83: mpwc_newline */
    Boolean f67;                                         /* 0x84: ignore_oldstyle */
    Boolean f68;                                         /* 0x85: cpp_extensions */
    UInt8 f69;                                           /* 0x86: pointercast_lvalue */
    char rttiEnabled;                                    /* 0x87: RTTI */
    UInt8 f6b;                                           /* 0x88: delete_exception */
    UInt8 field_0x89;                                    /* 0x89 */
    UInt8 oldalignment;                                  /* 0x8a: oldalignment */
    UInt8 unsignedChar;                                  /* 0x8b: unsigned_char */
    char f6f;                                            /* 0x8c: multibyteaware */
    UInt8 f70;                                           /* 0x8d: auto_inline */
    UInt8 f71;                                           /* 0x8e: defer_codegen */
    UInt8 f72;                                           /* 0x8f: direct_to_som */
    UInt8 f73;                                           /* 0x90: SOMCheckEnvironment */
    UInt8 f74;                                           /* 0x91: SOMCallOptimization */
    UInt8 f75;                                           /* 0x92: bool */
    UInt8 old_enum_mangler;                              /* 0x93: old_enum_mangler */
    UInt8 f77;                                           /* 0x94: longlong */
    UInt8 f78;                                           /* 0x95: longlong_enums */
    UInt8 no_tfuncinline;                                /* 0x96: no_tfuncinline */
    UInt8 f7a;                                           /* 0x97 */
    char f7b;                                            /* 0x98: flat_include */
    char f7c;                                            /* 0x99: syspath_once */
    char f7d;                                            /* 0x9a: always_import */
    UInt8 f7e;                                           /* 0x9b: simple_class_byval */
    char f7f;                                            /* 0x9c: wchar_type */
    UInt8 f80;                                           /* 0x9d: vbase_ctor_offset */
    char f81;                                            /* 0x9e: vbase_abi_v2 */
    char f82;                                            /* 0x9f: def_inherited */
    UInt8 f83;                                           /* 0xa0: template_patch */
    UInt8 f84;                                           /* 0xa1: template_friends */
    Boolean f85;                                         /* 0xa2: faster_pch_gen */
    UInt8 f86;                                           /* 0xa3: array_new_delete */
    UInt8 f87;                                           /* 0xa4: dollar_identifiers */
    UInt8 def_inline_tfuncs;                             /* 0xa5: def_inline_tfuncs */
    UInt8 f89;                                           /* 0xa6: arg_dep_lookup */
    char f8a;                                            /* 0xa7: simple_prepdump */
    char f8b;                                            /* 0xa8: line_prepdump */
    UInt8 f8c;                                           /* 0xa9: fullpath_prepdump */
    UInt8 old_mtemplparser;                              /* 0xaa: old_mtemplparser */
    UInt8 f8e;                                           /* 0xab: suppress_init_code */
    Boolean f8f;                                         /* 0xac: reverse_bitfields */
    UInt8 f90;                                           /* 0xad: c9x */
    Boolean f91;                                         /* 0xae: float_constants */
    Boolean f92;                                         /* 0xaf: no_static_dtors */
    Boolean f93;                                         /* 0xb0: longlong_prepeval */
    UInt8 f94;                                           /* 0xb1: const_strings */
    UInt8 dumpir;                                        /* 0xb2: dumpir */
    UInt8 f96;                                           /* 0xb3: experimental */
    UInt8 f97;                                           /* 0xb4: gcc_extensions */
    UInt8 stdc_fp_contract;                              /* 0xb5: stdc_fp_contract */
    UInt8 stdc_fenv_access;                              /* 0xb6: stdc_fenv_access */
    UInt8 stdc_cx_limitedr;                              /* 0xb7: stdc_cx_limitedr */
    UInt8 old_argmatch;                                  /* 0xb8: old_argmatch */
    UInt8 optEH;                                         /* 0xb9: optEH */
    UInt8 optEH2;                                        /* 0xba: optEH2 */
    UInt8 new_mangler;                                   /* 0xbb: new_mangler */
    UInt8 fullpath_file;                                 /* 0xbc: fullpath_file */
    UInt8 microsoft_exceptions;                          /* 0xbd: microsoft_exceptions */
    UInt8 stdc_libfuncname_redefined;                    /* 0xbe: stdc_libfuncname_redefined */
    char f9c;                                            /* 0xbf: warning_errors */
    char f9d;                                            /* 0xc0: extended_errorcheck */
    UInt8 f9e;                                           /* 0xc1: check_header_flags */
    UInt8 supress_warnings;                              /* 0xc2: supress_warnings */
    Boolean f9f;                                         /* 0xc3: warn_illpragma */
    UInt8 fa0;                                           /* 0xc4: warn_emptydecl */
    Boolean fa1;                                         /* 0xc5: warn_possunwant */
    char fa2;                                            /* 0xc6: warn_unusedvar */
    char fa3;                                            /* 0xc7: warn_unusedarg */
    UInt8 fa4;                                           /* 0xc8: warn_extracomma */
    Boolean fa5;                                         /* 0xc9: warn_hidevirtual */
    char fa6;                                            /* 0xca: warn_largeargs */
    Boolean fa7;                                         /* 0xcb: warn_implicitconv */
    UInt8 fa8;                                           /* 0xcc: warn_notinlined */
    SInt8 fa9;                                           /* 0xcd: warn_structclass */
    Boolean faa;                                         /* 0xce: warn_padding */
    Boolean fab;                                         /* 0xcf: warn_no_side_effect */
    Boolean fac;                                         /* 0xd0: warn_resultnotused */
    UInt8 warn_ptr_int_conv;                             /* 0xd1: warn_ptr_int_conv */
    UInt8 warn_uninitializedvar;                         /* 0xd2: warn_uninitializedvar */
    signed char structalignment;                         /* 0xd3 */
    UInt8 arrayAlignment;                                /* 0xd4: align_array_members */
    SInt8 faf;                                           /* 0xd5: dont_reuse_strings */
    UInt8 fb0;                                           /* 0xd6: pool_strings */
    UInt8 fb1;                                           /* 0xd7: explicit_zero_data */
    UInt8 fb2;                                           /* 0xd8: readonly_strings */
    UInt8 fb3;                                           /* 0xd9: exceptions */
    UInt8 field_0xda;                                    /* 0xda */
    UInt8 disableInlining;                               /* 0xdb: dont_inline */
    UInt8 fb6;                                           /* 0xdc: always_inline */
    char peepholeOptimizationEnabled;                    /* 0xdd: peephole */
    Boolean irOptimizationEnabled;                       /* 0xde: global_optimizer */
    unsigned char fb9;                                   /* 0xdf: side_effects */
    UInt8 fbb;                                           /* 0xe0: internal */
    UInt8 fbc;                                           /* 0xe1: import */
    UInt8 fbd;                                           /* 0xe2: export */
    UInt8 fbe;                                           /* 0xe3: lib_export */
    char fbf;                                            /* 0xe4: nosyminline */
    char fc0;                                            /* 0xe5: force_active */
    UInt8 block_assign;                                  /* 0xe6: block_assign */
    UInt8 has8bytebitfields;                             /* 0xe7: has8bytebitfields */
    SInt8 deleteDeadInstructions;                        /* 0xe8 */
    UInt8 uniformSpillBlockWeight;                       /* 0xe9: optimize_for_size */
    UInt8 fc3;                                           /* 0xea */
    UInt8 field_0xeb;                                    /* 0xeb */
    UInt8 irCommonSubexpressionElimination;              /* 0xec: opt_common_subs */
    UInt8 fc5;                                           /* 0xed: opt_loop_invariants */
    UInt8 irCopyPropagation;                             /* 0xee: opt_propagation */
    UInt8 irEliminateUnused;                             /* 0xef: opt_dead_assignments */
    UInt8 fc8;                                           /* 0xf0: opt_strength_reduction */
    UInt8 fc9;                                           /* 0xf1: opt_strength_reduction_strict */
    UInt8 irRemoveUnreachable;                           /* 0xf2: opt_dead_code */
    UInt8 fcb;                                           /* 0xf3: opt_lifetimes */
    UInt8 field_0xf4;                                    /* 0xf4 */
    UInt8 irLoopUnrolling;                               /* 0xf5: opt_unroll_loops */
    Boolean fce;                                         /* 0xf6: opt_vectorize_loops */
    UInt8 irSecondOptimizationPass;                      /* 0xf7 */
    UInt8 opt_pointer_analysis;                          /* 0xf8: opt_pointer_analysis */
    UInt8 field_0xf9;                                    /* 0xf9 */
    signed char unrollOption;                            /* 0xfa */
    SInt8 fd1;                                           /* 0xfb */
    UInt8 filesyminfo;                                   /* 0xfc: sym */
    UInt8 field_0xfd;                                    /* 0xfd */
    char fd4;                                            /* 0xfe */
    UInt8 fd5;                                           /* 0xff */
    Boolean fd6;                                         /* 0x100 */
    UInt8 field_0x101;                                   /* 0x101 */
    int precompiledHeaderCreator;                        /* 0x102 */
    UInt32 precompiledHeaderFileTypes[2];                /* 0x106 */
    HashNameNode *fe4;                                   /* 0x10e */
#else
    UInt8 nativeByteOrder;
    UInt8 pad01[1];
    SInt16 processor;
    SInt8 processorModel; /* 0x04: SetProcessorModel stores model; Scheduler selects target scheduling model */
    UInt8 instructionSchedulingMode; /* 0x05: CodeGen selects Scheduler_Schedule when mode is 2 */
    unsigned char cOptimizerDumpEnabled;
    char emitExtraAssemblyData; /* 0x07: PCodeAssembly_ShouldEmitExtraData */
    Boolean f08;
    char
        debugOptions; /* 0x09: TargetPanels_eabi_ppc_LoadCompilerOptions clears this, then loads CompilerOptions.debugOptions for enabled, non-operand debugging. */
    Boolean ppcUnrollSpeculative;
    UInt8 pad0b[1];
    SInt16 ppcUnrollInstructionsLimit;
    SInt16 ppcUnrollFactorLimit;
    UInt8 altivecModel;
    UInt8 altivecVrsave; /* 0x11: CodeGen parses altivec_vrsave; StackFrameEABI gates VRSAVE frame handling */
    UInt8 codeAlignment; /* 0x12: SetCodeAlignment; BE_symbol assigns default symbol alignment */
    char emitSerializedAssemblyFormat; /* 0x13: PCodeAssembly_ShouldEmitSerializedFormat */
    char f14;
    UInt8 pad15[1];
    SInt32 smallDataLimit; /* 0x16: ObjGen_PPC_EABI_SetObjectSection tests initialized data size */
    int smallBSSLimit;     /* 0x1a: ObjGen_PPC_EABI_SetObjectSection tests uninitialized data size */
    UInt8 f1e;
    UInt8 reuseSectionSymbols; /* 0x1f: BE_symbol_004913b0 gates reuse of section symbolLink data */
    UInt8 f20;
    UInt8 pad21[1];
    UInt8 useRegisterSaveHelpers; /* 0x22: StackFrameEABI selects useSaveHelper for register saves */
    char debugEnabled;
    Boolean operandsDebug;
    UInt8 f25;
    UInt8 f26;
    char f27;
    SInt8 rel109Offset; /* 0x28: CodeGen parses rel109_offset; ObjGen_PPC_EABI adds it to relocation offset */
    UInt8 pad29[1];
    struct ObjGenSection
        *textSection; /* 0x2a: ObjGen_PPC_EABI_SetObjectSection selects section for function datatypes */
    struct ObjGenSection *dataSection;      /* 0x2e: ObjGen_PPC_EABI_DefaultDataSectionIndex */
    struct ObjGenSection *bssSection;       /* 0x32: ObjGen_PPC_EABI_DefaultBSSSectionIndex */
    struct ObjGenSection *smallDataSection; /* 0x36: ObjGen_PPC_EABI_SetObjectSection selects initialized small data */
    struct ObjGenSection *smallBSSSection; /* 0x3a: ObjGen_PPC_EABI_SetObjectSection selects uninitialized small data */
    SInt32 sectionHeaderTableSize;
    struct SectionSymbolAttributes **sectionHeaderTable;
    struct InterruptList *interruptList;
    struct InterruptGenerationRecord *interruptOptions; /* 0x4a: CodeGen_ParsePragma saves active interrupt options */
    UInt8 f4e;
    Boolean emitMainFileObject; /* 0x4f: ObjGen_PPC_EABI_FinalizeOutputBuffers gates create_main_file_object */
    UInt8
        returnStructsInMemory; /* 0x50: Type_RequiresMemoryReturn forces memory return even for aggregates of size at most 8 */
    UInt8 pad51[3];
    char *f54;
    SInt16 inlineLimit;
    UInt8 cplusplus;
    UInt8 f5b;
    UInt8 f5c;
    SInt8 f5d;
    char f5e;
    char f5f;
    Boolean f60;
    char trigraphs;
    UInt8 f62;
    UInt8 f63;
    UInt8 rejectZeroLengthArrayMembers;
    UInt8 f65;
    UInt8 f66;
    Boolean f67;
    Boolean f68;
    UInt8 f69;
    char rttiEnabled;
    UInt8 f6b;
    UInt8 pad6c[2];
    UInt8 unsignedChar;
    char f6f;
    UInt8 f70;
    UInt8 f71;
    UInt8 f72;
    UInt8 f73;
    UInt8 f74;
    UInt8 f75;
    UInt8 pad76[1];
    UInt8 f77;
    UInt8 f78;
    UInt8 pad79[1];
    UInt8 f7a;
    char f7b;
    char f7c;
    char f7d;
    UInt8 f7e;
    char f7f;
    UInt8 f80;
    char f81;
    char f82;
    UInt8 f83;
    UInt8 f84;
    Boolean f85;
    UInt8 f86;
    UInt8 f87;
    UInt8 pad88[1];
    UInt8 f89;
    volatile char f8a;
    char f8b;
    UInt8 f8c;
    UInt8 pad8d[1];
    UInt8 f8e;
    Boolean f8f;
    UInt8 f90;
    Boolean f91;
    Boolean f92;
    Boolean f93;
    UInt8 f94;
    UInt8 pad95[1];
    UInt8 f96;
    UInt8 f97;
    UInt8 pad98[4];
    char f9c;
    char f9d;
    UInt8 f9e;
    Boolean f9f;
    UInt8 fa0;
    Boolean fa1;
    char fa2;
    char fa3;
    UInt8 fa4;
    Boolean fa5;
    char fa6;
    Boolean fa7;
    UInt8 fa8;
    SInt8 fa9;
    Boolean faa;
    Boolean fab;
    Boolean fac;
    signed char structalignment;
    UInt8 arrayAlignment;
    SInt8 faf;
    UInt8 fb0;
    UInt8 fb1;
    UInt8 fb2;
    UInt8 fb3;
    UInt8 padb4[1];
    UInt8 disableInlining;
    UInt8 fb6;
    char
        peepholeOptimizationEnabled; /* 0xb7: CodeGen gates Peephole_MergeAdjacentBlocks and Peephole_VisitBlocksWithMultipleInstructions */
    Boolean irOptimizationEnabled;
    unsigned char fb9;
    UInt8 fba;
    UInt8 fbb;
    UInt8 fbc;
    UInt8 fbd;
    UInt8 fbe;
    char fbf;
    char fc0;
    SInt8 deleteDeadInstructions;
    UInt8 uniformSpillBlockWeight;
    UInt8 fc3;
    UInt8
        irCommonSubexpressionElimination; /* 0xc4: IRO_Optimizer gates IRO_CommonSubs and available-expression computation */
    UInt8 fc5;
    UInt8 irCopyPropagation; /* 0xc6: IRO_CopyPropagationSetting */
    UInt8 irEliminateUnused; /* 0xc7: IRO_Optimizer passes eliminateUnused to IRO_UseDef */
    UInt8 fc8;
    UInt8 fc9;
    UInt8 irRemoveUnreachable; /* 0xca: IRO_Optimizer gates IRO_RemoveUnreachable */
    UInt8 fcb;
    UInt8 padcc[1];
    UInt8 irLoopUnrolling; /* 0xcd: IRO_Optimizer gates IRO_LoopUnroller */
    Boolean fce;
    UInt8 irSecondOptimizationPass; /* 0xcf: IRO_Optimizer selects two passes rather than one */
    signed char unrollOption;
    SInt8 fd1;
    UInt8 filesyminfo;
    UInt8 fd3;
    char fd4;
    UInt8 fd5;
    Boolean fd6;
    UInt8 padd7[1];
    int precompiledHeaderCreator;
    UInt32 precompiledHeaderFileTypes[2];
    HashNameNode *fe4;
#endif
};
#pragma options align = reset
#pragma options align = mac68k
struct CPrepCU {
    struct CWPluginPrivateContext *context; /* 0x00: dispatch_compiler_plugin_request supplies its plugin context. */
    SInt32 objectData;
    SInt32 browseData;
    UInt8 pad0c[4];
    SInt32 codeSize;
    SInt32 udataSize;
    SInt32 idataSize;
    SInt32 lineCount; /* 0x1c: CPrep_Compile stores line_count. */
    UInt8 pad20[0x14];
    SInt32 objectBuffer;
    SInt32 browseBuffer;
    SInt32 pluginRequest;
    SInt32 apiVersion;
    struct CWFileSpec projectFile;
    SInt32 projectFileCount;
    SInt32 mainFileNumber;
    struct CWFileSpec mainFile;
    UInt32 mainFileOffset;
    UInt32 mainFileLength;
    UInt8 precompiling;
    UInt8 active; /* 0xe1: initialize_compiler_plugin_cu obtains CPrep_GetActive. */
    UInt8 preprocessOnly;
    UInt8 filesyminfo;
    UInt8 useMappedPrecompiledHeaders;
    UInt8 pad_e5;
    struct BrowseOptions
        browseOptions; /* 0xe6: initialize_compiler_plugin_cu obtains the browse settings from CPrep_GetDependencyState. */
    UInt8 compiling;
    UInt8
        mainFileAttributesAlignment; /* 0xf7: initialize_compiler_plugin_cu clears this byte; it aligns the UInt16 mainFileAttributes read by CPrep_GetDependencyOption at 0xf8. */
    UInt16 mainFileAttributes;
    SInt32 platformCode1; /* 0xfa: initialize_compiler_plugin_cu copies head.platformCodes[1]. */
    SInt32 platformCode0; /* 0xfe: initialize_compiler_plugin_cu copies head.platformCodes[0]. */
};
#pragma options align = reset
#pragma options align = mac68k
struct CPrepRec {
    struct PFile *file;
    SInt32 pos;
    SInt16 state;
};
#pragma options align = reset
#pragma options align = mac68k
struct CPrepValue {
    CInt64 value;
    Boolean isUnsigned;
};
#pragma options align = reset
#pragma options align = mac68k
struct CPrep_0043afc0_Entry {
    struct HashNameNode *obj;
    SInt16 align;
};
#pragma options align = reset
struct ObjectCallbackContext {
    char reserved[0x10];
    unsigned int(__stdcall *callback)(struct CWPluginPrivateContext *, unsigned int);
    char reserved14[8];
    unsigned int(__stdcall *invoke)(struct CWPluginPrivateContext *, struct FileProcessingInfo *, const char *);
};
#pragma options align = mac68k
struct IncludeFilenamePointer {
    char *filename;
};
#pragma options align = reset
#pragma options align = mac68k
struct IncludeSearchPolicy {
    char searchLocal; /* 0x00: CPrep.c sets local search for quoted includes and tests it for the macro-key prefix. */
    char unusedBytes[3]; /* 0x01: CPrep.c accesses only searchLocal; these bytes have no reads or writes. */
};
#pragma options align = reset
#pragma options align = mac68k
struct Macro {
    struct Macro *next;
    struct HashNameNode *name;
    char *text;
    UInt16 nargs;
    UInt8 flag;
    UInt8 isExpanding; /* 0x0f: find_expandable_macro rejects active expansions; CPrep_PopState clears it */
    struct HashNameNode *args[1];
};
#pragma options align = reset
#pragma options align = mac68k
struct MacroStack {
    char *pos;
    char *macname;
    struct Macro *macro;
    Boolean macrocheck;
};
#pragma options align = reset
#pragma options align = mac68k
struct OptionEntry {
    char *name;
    SInt16 flags;
};
#pragma options align = reset
#pragma options align = mac68k
struct PFile {
    CWFileSpec header; /* 0x00: fn_004401b0 copies the resolved file specification */
#if VERSION >= VERSION_GC_2_7
    HashNameNode *nameNode;
    SInt32 fileModDate;
    SInt32 lineOffset;
#endif
    char *textstart;
    SInt32 textlength;
    SInt32 linenumber;
    SInt32 pos;
    Boolean hasprepline;
    UInt8 fileIDAlignment; /* 0x57: fn_004401b0 clears whole PFile; unused byte aligns the following SInt16 fileID */
    SInt16 fileID;
    Boolean recordbrowseinfo;
    UInt8 unusedBytes[3]; /* 0x5b legacy, 0x67 GC 2.7: cleared with the whole PFile; no member reads or writes */
    Boolean isDefault;
};
#pragma options align = reset
#pragma options align = mac68k
struct PragmaNode {
    struct PragmaNode *next;
    COpts rec;
};
#pragma options align = reset
#pragma options align = mac68k
struct PragmaSettings {
    unsigned char reserved[226];
    unsigned char mode;
};
#pragma options align = reset
#pragma options align = mac68k
struct PrepNameCacheEntry {
    struct PrepNameCacheEntry *next;
    struct PFile *name;
#if VERSION < VERSION_GC_2_7
    HashNameNode *value;
    SInt32 auxiliaryValue;
#endif
};
#pragma options align = reset
struct PrepTokenBuffer {
    int count;
    struct SavedPrepToken *tokens;
};
extern VarInfo *CPrep_AllocateVarInfo(void);
extern PFile *CPrep_GetPFile(void);
extern void parse_elif_directive(void);
extern void parse_ifndef(void);
extern void parse_ifdef_directive(void);
extern void parse_if_directive(void);
extern CNameRef evaluate_conditional_expression_value(void);
extern Boolean is_zero_name_ref(CNameRef *value);
extern void parse_line_directive(void);
extern void CPrep_0043a0a0(char allowInclude);
extern void parse_pragma(void);
extern void concatenate_and_dispatch_string_tokens(void);
extern void parse_structalignment(void);
extern void parse_prep_setting(void);
extern void parse_unroll_pragma(void);
extern void parse_optimization_level_pragma(void);
extern void CPrep_0043b790(void);
extern void parse_align_pragma(void);
extern void parse_inline_limit(void);
extern void parse_pragma_option(int directive);
extern char *expand_builtin_macro(Macro *obj);
extern char *CPrep_GetFileName(char *param1, Boolean param2, Boolean param3);
extern char *expand_macros_in_text(Macro *state, char *text);
extern Boolean evaluate_pragma_option(void);
extern void define_macro(void);
extern SInt16 CPrep_ScanMacroExpandedChar(void);
extern void skip_line_breaks_and_expand_macros(void);
extern unsigned int lookup_available_macro(HashNameNode *name);
extern Macro *find_macro_for_expansion(UInt8 *p);
extern Macro *find_expandable_macro(UInt8 *text);
extern Boolean CPrep_0043ecb0(short ch);
extern void CPrep_PopMacro(void);
extern UInt8 CPrep_Compile(CPrepCU *cu);
extern SInt32 CPrep_UpdateTokenLine(FOI *foi);
extern void CPrep_PopFile(void);
extern BufferedToken *CPrep_GetLastBufferedToken(void);
extern int CPrep_0043f860(char *p);
extern void CPrep_ResetBufferedTokenPosition(void);
extern void fn_0043f3b0(short code);
extern NameSpaceList *CPrep_ReportError(short token);
extern void CPrep_RemoveBufferedTokens(int *entryCount, SInt32 *firstIndex);
extern void CPrep_InsertTokenBuffer(PrepTokenBuffer *arg, SInt32 *result);
extern void CPrep_BufferTokensThroughSemicolon(PrepTokenBuffer *buffer, void (*processToken)(struct BufferedToken *));
extern void CPrep_SaveFunctionBodyTokens(PrepTokenBuffer *result, void (*tokenCallback)(BufferedToken *), int option);
extern void CPrep_SetPosition(SInt32 *position);
extern void CPrep_UngetToken(void);
extern void CPrep_IncrementCountersAndUpdateTextOffset(void);
extern void read_pragma_token(void);
extern void fn_0043be10(void);
extern void CPrep_RestoreOption(int key);
extern SInt16 CPrep_ExpectEndLine(char suppressDiagnostic);
extern UInt8 *find_identifier_end_after_optional_paren(UInt8 *text);
extern Macro *find_macro(void);
extern void fn_0043f3e0(unsigned int token, char *name);
extern void fn_004392e0(void);
extern void fn_0043f1f0(FOI *name);
extern void CPrep_SaveAndSetOption(unsigned int index, UInt8 value);
extern void undefine_macro(void);
extern void fn_0043e8f0(void);
extern void parse_else_directive(void);
extern void apply_pragma_object_flags(unsigned int flags);
extern UInt8 CPrep_ExpandMacro(void);
extern Macro *lookup_expandable_macro(void);
extern void CPrep_GetFOI(FOI *location, BufferedToken *record);
extern void CPrep_GetPosition(PFile **position, SInt32 *offset);
extern UInt8 *expand_macro(Macro *m);
extern int scan_braced_tokens(void (*callback)(BufferedToken *), int tokenCount);
extern struct CNameRef evaluate_binary_expression_value(struct CNameRef *init, SInt16 minprec);
extern struct CNameRef evaluate_unary_expression_value(void);
extern void CPrep_GetBrowseFilePosition(PFile **file, SInt32 *ppos);
extern void CPrep_ParseDirective(void);
extern int parse_endif_directive(void);
extern struct CPrepRec data_0057f6c8[];
extern SInt16 if_depth;
extern struct PrepNameCacheEntry *DAT_0057f9ca;
extern UInt8 DAT_0057f9dc;
extern UInt8 DAT_0057f9de;
extern struct CPrep_0043afc0_Entry saved_structalignments[];
extern UInt8 DAT_0057fcee;
extern GList macro_text;
extern short DAT_0057fd6c;
extern PFile *DAT_005875f8;
extern int DAT_00587ef0;
extern int remainingBufferedTokenCount;
extern UInt8 DAT_0058850f;
extern UInt8 DAT_00588516;
extern UInt8 DAT_00588523;
extern SInt16 macro_expansion_depth;
extern UInt32 intconst_lo;
extern struct Macro data_0054fd58;
extern struct Macro cplusplus_predefined_macro;
extern struct Macro data_0054feac;
extern struct Macro data_0054fec0;
extern char scheduling_name[11];
extern char floatingpoint[14];
extern char sfp_emulation[14];
extern char precompile_name[11];
extern char data_00550e4c[11];
extern struct PFile *data_0057f94a[32];
extern UInt32 next_scaled_ticks;
extern UInt8 data_0057f9d2;
extern UInt8 data_0057f9d3;
extern SInt32 data_0057f9d4;
extern SInt32 text_offset;
extern UInt8 data_0057f9dd;
extern SInt16 data_0057fce0;
extern struct PragmaNode *pragma_list;
extern struct IROOptNode *saved_options;
extern struct COpts *data_0057fcea;
extern struct BufferedToken *buffered_tokens;
extern SInt32 buffered_token_capacity;
extern SInt32 data_0057fd0c;
extern UInt8 f87_enabled;
extern struct Macro **macro_buckets;
extern SInt32 line_count;
extern struct BufferedToken *bufferedTokenPosition;
extern UInt8 *token_start;
extern struct PFile *currentPFile;
#if VERSION >= VERSION_GC_2_7
extern PFile *data_005df25c;
#endif
extern UInt8 *cprep_cu;
extern char *macro_text_start;
extern short current_file_index;
extern SInt16 data_00588470;
extern Boolean data_005884fd;
extern UInt8 data_0058850d;
extern UInt8 data_00588531;
extern MacroStack macro_stack[];
extern OptionEntry pragma_options[];
extern SInt32 CPrep_GetCurrentTextOffset(void);
extern void skip_inactive_if_blocks(void);
extern unsigned char fn_004401b0(unsigned char *name, unsigned char mode, unsigned char skip);
extern unsigned char file_cannot_opened_name[];
extern void CPrep_SetBufferedTokenPosition(SInt32 *count);
extern void CPrep_GetBufferedTokenPosition(SInt32 *count);
extern void CPrep_GrowBufferedTokenBuffer(SInt32 n);
extern void pop_files_and_release_heaps_and_lists(void);
extern int initialize_preprocessor(void);
extern unsigned int total_heap_size;
extern UInt8 DAT_00586fd0[256];
extern UInt8 data_0058702f;
extern struct BufferedToken *buffered_token_buffer_end;
extern UInt8 data_0058852a;
extern struct StorageHandle *buffered_token_storage;
extern SInt32 __stdcall CPrep_GetTargetSettings(CWPluginPrivateContext *obj, TgtRec *dst);
extern unsigned int __stdcall CPrep_CallCompilerCallbackWithValue(CWPluginPrivateContext *object_id,
                                                                  unsigned int argument, long *value);
extern SInt32 __stdcall CPrep_CallCompilerCallback(void *objectId, SInt32 argument);
extern unsigned int __stdcall call_compiler_callback(CWPluginPrivateContext *object_id, void *argument,
                                                     unsigned int result);
extern unsigned int *CPrep_RemoveFlaggedMacros(void);
extern CWPluginPrivateContext *validate_context_signature(CWPluginPrivateContext *record);
extern int CPrep_AppendStringBounded(char *dst, char *src, int size);
extern void __stdcall CPrep_RegisterPredefinedMacros(void);
extern int __stdcall CPrep_GetEnabled(int key, unsigned char *value);
extern unsigned int __stdcall CPrep_GetActive(int selector, unsigned char *value);
extern unsigned int __stdcall CPrep_GetOperation(int key, unsigned char *value);
extern unsigned int __stdcall CPrep_GetSetting(CWPluginPrivateContext *key, unsigned char *value);
extern unsigned int __stdcall CPrep_GetReserved15d(int handle, unsigned char *value);
extern unsigned int __stdcall CPrep_GetDependencyState(CWPluginPrivateContext *a0, BrowseOptions *a1);
extern unsigned int __stdcall CPrep_GetFileIndex(CWPluginPrivateContext *key, unsigned int *value);
extern unsigned int __stdcall CPrep_GetDependencyOption(int lookupKey, unsigned short *value);
extern int __stdcall CPrep_GetContextPayload(CWPluginPrivateContext *handle, CWFileSpec *destination);
extern unsigned int __stdcall CPrep_GetResultValues(CWPluginPrivateContext *handle, UInt32 *first_value,
                                                    UInt32 *second_value);
extern unsigned int __stdcall CPrep_InvokeCompilerCallback(CWPluginPrivateContext *instance_id,
                                                           struct FileProcessingInfo *argument, const char *value);
extern HashNameNode *fn_00441850(CWFileSpec *file, SInt32 *fileModDate);
extern struct Macro data_0054fd08;
extern struct Macro data_0054fd1c;
extern struct Macro data_0054fd30;
extern struct Macro data_0054fd44;
extern struct Macro data_0054fd6c;
extern struct Macro data_0054fd80;
extern struct Macro data_0054fd94;
extern struct Macro data_0054fda8;
extern struct Macro data_0054fdd0;
extern struct Macro data_0054fde4;
extern struct Macro data_0054fdf8;
extern struct Macro data_0054fe0c;
extern struct Macro data_0054fe20;
extern struct Macro optionalNameMacro;
extern struct Macro data_0054fe48;
extern struct Macro data_0054fe5c;
extern struct Macro data_0054fe70;
extern struct Macro data_0054fe84;
extern struct Macro data_0054fe98;
extern struct Macro data_0054fed4;
struct PFile;
extern struct BufferedToken lastBufferedToken;
struct ObjectCallbackContext;
struct ObjectCallbackContext;

#ifdef __cplusplus
}
#endif

#endif
