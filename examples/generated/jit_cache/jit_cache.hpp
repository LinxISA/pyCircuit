// pyCircuit C++ emission (prototype)
#include <pyc/cpp/pyc_sim.hpp>

namespace pyc::gen {

struct JitCache {
  pyc::cpp::Wire<1> sys_clk{};
  pyc::cpp::Wire<1> sys_rst{};
  pyc::cpp::Wire<1> req_valid{};
  pyc::cpp::Wire<32> req_addr{};
  pyc::cpp::Wire<1> rsp_ready{};
  pyc::cpp::Wire<1> req_ready{};
  pyc::cpp::Wire<1> rsp_valid{};
  pyc::cpp::Wire<1> rsp_hit{};
  pyc::cpp::Wire<32> rsp_rdata{};

  pyc::cpp::Wire<3> pyc_constant_1{};
  pyc::cpp::Wire<3> pyc_constant_2{};
  pyc::cpp::Wire<3> pyc_constant_3{};
  pyc::cpp::Wire<3> pyc_constant_4{};
  pyc::cpp::Wire<3> pyc_constant_5{};
  pyc::cpp::Wire<3> pyc_constant_6{};
  pyc::cpp::Wire<3> pyc_constant_7{};
  pyc::cpp::Wire<3> pyc_constant_8{};
  pyc::cpp::Wire<27> pyc_constant_9{};
  pyc::cpp::Wire<1> pyc_constant_10{};
  pyc::cpp::Wire<4> pyc_constant_11{};
  pyc::cpp::Wire<32> pyc_constant_12{};
  pyc::cpp::Wire<1> pyc_constant_13{};
  pyc::cpp::Wire<1> req_valid__jit_cache__L166{};
  pyc::cpp::Wire<32> req_addr__jit_cache__L167{};
  pyc::cpp::Wire<1> rsp_ready__jit_cache__L168{};
  pyc::cpp::Wire<1> cache__wvalid0__jit_cache__L172{};
  pyc::cpp::Wire<32> cache__waddr0__jit_cache__L173{};
  pyc::cpp::Wire<32> cache__wdata0__jit_cache__L174{};
  pyc::cpp::Wire<4> cache__wstrb0__jit_cache__L175{};
  pyc::cpp::Wire<3> pyc_comb_14{};
  pyc::cpp::Wire<3> pyc_comb_15{};
  pyc::cpp::Wire<3> pyc_comb_16{};
  pyc::cpp::Wire<3> pyc_comb_17{};
  pyc::cpp::Wire<3> pyc_comb_18{};
  pyc::cpp::Wire<3> pyc_comb_19{};
  pyc::cpp::Wire<3> pyc_comb_20{};
  pyc::cpp::Wire<3> pyc_comb_21{};
  pyc::cpp::Wire<27> pyc_comb_22{};
  pyc::cpp::Wire<1> pyc_comb_23{};
  pyc::cpp::Wire<32> pyc_comb_24{};
  pyc::cpp::Wire<1> pyc_comb_25{};
  pyc::cpp::Wire<1> pyc_comb_26{};
  pyc::cpp::Wire<32> pyc_comb_27{};
  pyc::cpp::Wire<1> pyc_comb_28{};
  pyc::cpp::Wire<1> pyc_comb_29{};
  pyc::cpp::Wire<32> pyc_comb_30{};
  pyc::cpp::Wire<32> pyc_comb_31{};
  pyc::cpp::Wire<4> pyc_comb_32{};
  pyc::cpp::Wire<1> cache__req_q__in_valid{};
  pyc::cpp::Wire<32> cache__req_q__in_data{};
  pyc::cpp::Wire<1> cache__req_q__out_ready{};
  pyc::cpp::Wire<1> pyc_fifo_33{};
  pyc::cpp::Wire<1> pyc_fifo_34{};
  pyc::cpp::Wire<32> pyc_fifo_35{};
  pyc::cpp::Wire<1> cache__rsp_q__in_valid{};
  pyc::cpp::Wire<33> cache__rsp_q__in_data{};
  pyc::cpp::Wire<1> cache__rsp_q__out_ready{};
  pyc::cpp::Wire<1> pyc_fifo_36{};
  pyc::cpp::Wire<1> pyc_fifo_37{};
  pyc::cpp::Wire<33> pyc_fifo_38{};
  pyc::cpp::Wire<1> pyc_extract_39{};
  pyc::cpp::Wire<1> cache__rsp_hit__jit_cache__L183{};
  pyc::cpp::Wire<32> pyc_extract_40{};
  pyc::cpp::Wire<32> cache__rsp_rdata__jit_cache__L184{};
  pyc::cpp::Wire<1> pyc_comb_41{};
  pyc::cpp::Wire<32> pyc_comb_42{};
  pyc::cpp::Wire<1> cache__valid__s0__w0__next{};
  pyc::cpp::Wire<1> pyc_reg_43{};
  pyc::cpp::Wire<1> cache__valid__s0__w0{};
  pyc::cpp::Wire<1> cache__valid__s0__w1__next{};
  pyc::cpp::Wire<1> pyc_reg_44{};
  pyc::cpp::Wire<1> cache__valid__s0__w1{};
  pyc::cpp::Wire<1> cache__valid__s1__w0__next{};
  pyc::cpp::Wire<1> pyc_reg_45{};
  pyc::cpp::Wire<1> cache__valid__s1__w0{};
  pyc::cpp::Wire<1> cache__valid__s1__w1__next{};
  pyc::cpp::Wire<1> pyc_reg_46{};
  pyc::cpp::Wire<1> cache__valid__s1__w1{};
  pyc::cpp::Wire<1> cache__valid__s2__w0__next{};
  pyc::cpp::Wire<1> pyc_reg_47{};
  pyc::cpp::Wire<1> cache__valid__s2__w0{};
  pyc::cpp::Wire<1> cache__valid__s2__w1__next{};
  pyc::cpp::Wire<1> pyc_reg_48{};
  pyc::cpp::Wire<1> cache__valid__s2__w1{};
  pyc::cpp::Wire<1> cache__valid__s3__w0__next{};
  pyc::cpp::Wire<1> pyc_reg_49{};
  pyc::cpp::Wire<1> cache__valid__s3__w0{};
  pyc::cpp::Wire<1> cache__valid__s3__w1__next{};
  pyc::cpp::Wire<1> pyc_reg_50{};
  pyc::cpp::Wire<1> cache__valid__s3__w1{};
  pyc::cpp::Wire<1> cache__valid__s4__w0__next{};
  pyc::cpp::Wire<1> pyc_reg_51{};
  pyc::cpp::Wire<1> cache__valid__s4__w0{};
  pyc::cpp::Wire<1> cache__valid__s4__w1__next{};
  pyc::cpp::Wire<1> pyc_reg_52{};
  pyc::cpp::Wire<1> cache__valid__s4__w1{};
  pyc::cpp::Wire<1> cache__valid__s5__w0__next{};
  pyc::cpp::Wire<1> pyc_reg_53{};
  pyc::cpp::Wire<1> cache__valid__s5__w0{};
  pyc::cpp::Wire<1> cache__valid__s5__w1__next{};
  pyc::cpp::Wire<1> pyc_reg_54{};
  pyc::cpp::Wire<1> cache__valid__s5__w1{};
  pyc::cpp::Wire<1> cache__valid__s6__w0__next{};
  pyc::cpp::Wire<1> pyc_reg_55{};
  pyc::cpp::Wire<1> cache__valid__s6__w0{};
  pyc::cpp::Wire<1> cache__valid__s6__w1__next{};
  pyc::cpp::Wire<1> pyc_reg_56{};
  pyc::cpp::Wire<1> cache__valid__s6__w1{};
  pyc::cpp::Wire<1> cache__valid__s7__w0__next{};
  pyc::cpp::Wire<1> pyc_reg_57{};
  pyc::cpp::Wire<1> cache__valid__s7__w0{};
  pyc::cpp::Wire<1> cache__valid__s7__w1__next{};
  pyc::cpp::Wire<1> pyc_reg_58{};
  pyc::cpp::Wire<1> cache__valid__s7__w1{};
  pyc::cpp::Wire<27> cache__tag__s0__w0__next{};
  pyc::cpp::Wire<27> pyc_reg_59{};
  pyc::cpp::Wire<27> cache__tag__s0__w0{};
  pyc::cpp::Wire<27> cache__tag__s0__w1__next{};
  pyc::cpp::Wire<27> pyc_reg_60{};
  pyc::cpp::Wire<27> cache__tag__s0__w1{};
  pyc::cpp::Wire<27> cache__tag__s1__w0__next{};
  pyc::cpp::Wire<27> pyc_reg_61{};
  pyc::cpp::Wire<27> cache__tag__s1__w0{};
  pyc::cpp::Wire<27> cache__tag__s1__w1__next{};
  pyc::cpp::Wire<27> pyc_reg_62{};
  pyc::cpp::Wire<27> cache__tag__s1__w1{};
  pyc::cpp::Wire<27> cache__tag__s2__w0__next{};
  pyc::cpp::Wire<27> pyc_reg_63{};
  pyc::cpp::Wire<27> cache__tag__s2__w0{};
  pyc::cpp::Wire<27> cache__tag__s2__w1__next{};
  pyc::cpp::Wire<27> pyc_reg_64{};
  pyc::cpp::Wire<27> cache__tag__s2__w1{};
  pyc::cpp::Wire<27> cache__tag__s3__w0__next{};
  pyc::cpp::Wire<27> pyc_reg_65{};
  pyc::cpp::Wire<27> cache__tag__s3__w0{};
  pyc::cpp::Wire<27> cache__tag__s3__w1__next{};
  pyc::cpp::Wire<27> pyc_reg_66{};
  pyc::cpp::Wire<27> cache__tag__s3__w1{};
  pyc::cpp::Wire<27> cache__tag__s4__w0__next{};
  pyc::cpp::Wire<27> pyc_reg_67{};
  pyc::cpp::Wire<27> cache__tag__s4__w0{};
  pyc::cpp::Wire<27> cache__tag__s4__w1__next{};
  pyc::cpp::Wire<27> pyc_reg_68{};
  pyc::cpp::Wire<27> cache__tag__s4__w1{};
  pyc::cpp::Wire<27> cache__tag__s5__w0__next{};
  pyc::cpp::Wire<27> pyc_reg_69{};
  pyc::cpp::Wire<27> cache__tag__s5__w0{};
  pyc::cpp::Wire<27> cache__tag__s5__w1__next{};
  pyc::cpp::Wire<27> pyc_reg_70{};
  pyc::cpp::Wire<27> cache__tag__s5__w1{};
  pyc::cpp::Wire<27> cache__tag__s6__w0__next{};
  pyc::cpp::Wire<27> pyc_reg_71{};
  pyc::cpp::Wire<27> cache__tag__s6__w0{};
  pyc::cpp::Wire<27> cache__tag__s6__w1__next{};
  pyc::cpp::Wire<27> pyc_reg_72{};
  pyc::cpp::Wire<27> cache__tag__s6__w1{};
  pyc::cpp::Wire<27> cache__tag__s7__w0__next{};
  pyc::cpp::Wire<27> pyc_reg_73{};
  pyc::cpp::Wire<27> cache__tag__s7__w0{};
  pyc::cpp::Wire<27> cache__tag__s7__w1__next{};
  pyc::cpp::Wire<27> pyc_reg_74{};
  pyc::cpp::Wire<27> cache__tag__s7__w1{};
  pyc::cpp::Wire<32> cache__data__s0__w0__next{};
  pyc::cpp::Wire<32> pyc_reg_75{};
  pyc::cpp::Wire<32> cache__data__s0__w0{};
  pyc::cpp::Wire<32> cache__data__s0__w1__next{};
  pyc::cpp::Wire<32> pyc_reg_76{};
  pyc::cpp::Wire<32> cache__data__s0__w1{};
  pyc::cpp::Wire<32> cache__data__s1__w0__next{};
  pyc::cpp::Wire<32> pyc_reg_77{};
  pyc::cpp::Wire<32> cache__data__s1__w0{};
  pyc::cpp::Wire<32> cache__data__s1__w1__next{};
  pyc::cpp::Wire<32> pyc_reg_78{};
  pyc::cpp::Wire<32> cache__data__s1__w1{};
  pyc::cpp::Wire<32> cache__data__s2__w0__next{};
  pyc::cpp::Wire<32> pyc_reg_79{};
  pyc::cpp::Wire<32> cache__data__s2__w0{};
  pyc::cpp::Wire<32> cache__data__s2__w1__next{};
  pyc::cpp::Wire<32> pyc_reg_80{};
  pyc::cpp::Wire<32> cache__data__s2__w1{};
  pyc::cpp::Wire<32> cache__data__s3__w0__next{};
  pyc::cpp::Wire<32> pyc_reg_81{};
  pyc::cpp::Wire<32> cache__data__s3__w0{};
  pyc::cpp::Wire<32> cache__data__s3__w1__next{};
  pyc::cpp::Wire<32> pyc_reg_82{};
  pyc::cpp::Wire<32> cache__data__s3__w1{};
  pyc::cpp::Wire<32> cache__data__s4__w0__next{};
  pyc::cpp::Wire<32> pyc_reg_83{};
  pyc::cpp::Wire<32> cache__data__s4__w0{};
  pyc::cpp::Wire<32> cache__data__s4__w1__next{};
  pyc::cpp::Wire<32> pyc_reg_84{};
  pyc::cpp::Wire<32> cache__data__s4__w1{};
  pyc::cpp::Wire<32> cache__data__s5__w0__next{};
  pyc::cpp::Wire<32> pyc_reg_85{};
  pyc::cpp::Wire<32> cache__data__s5__w0{};
  pyc::cpp::Wire<32> cache__data__s5__w1__next{};
  pyc::cpp::Wire<32> pyc_reg_86{};
  pyc::cpp::Wire<32> cache__data__s5__w1{};
  pyc::cpp::Wire<32> cache__data__s6__w0__next{};
  pyc::cpp::Wire<32> pyc_reg_87{};
  pyc::cpp::Wire<32> cache__data__s6__w0{};
  pyc::cpp::Wire<32> cache__data__s6__w1__next{};
  pyc::cpp::Wire<32> pyc_reg_88{};
  pyc::cpp::Wire<32> cache__data__s6__w1{};
  pyc::cpp::Wire<32> cache__data__s7__w0__next{};
  pyc::cpp::Wire<32> pyc_reg_89{};
  pyc::cpp::Wire<32> cache__data__s7__w0{};
  pyc::cpp::Wire<32> cache__data__s7__w1__next{};
  pyc::cpp::Wire<32> pyc_reg_90{};
  pyc::cpp::Wire<32> cache__data__s7__w1{};
  pyc::cpp::Wire<1> cache__rr__s0__next{};
  pyc::cpp::Wire<1> pyc_reg_91{};
  pyc::cpp::Wire<1> cache__rr__s0{};
  pyc::cpp::Wire<1> cache__rr__s1__next{};
  pyc::cpp::Wire<1> pyc_reg_92{};
  pyc::cpp::Wire<1> cache__rr__s1{};
  pyc::cpp::Wire<1> cache__rr__s2__next{};
  pyc::cpp::Wire<1> pyc_reg_93{};
  pyc::cpp::Wire<1> cache__rr__s2{};
  pyc::cpp::Wire<1> cache__rr__s3__next{};
  pyc::cpp::Wire<1> pyc_reg_94{};
  pyc::cpp::Wire<1> cache__rr__s3{};
  pyc::cpp::Wire<1> cache__rr__s4__next{};
  pyc::cpp::Wire<1> pyc_reg_95{};
  pyc::cpp::Wire<1> cache__rr__s4{};
  pyc::cpp::Wire<1> cache__rr__s5__next{};
  pyc::cpp::Wire<1> pyc_reg_96{};
  pyc::cpp::Wire<1> cache__rr__s5{};
  pyc::cpp::Wire<1> cache__rr__s6__next{};
  pyc::cpp::Wire<1> pyc_reg_97{};
  pyc::cpp::Wire<1> cache__rr__s6{};
  pyc::cpp::Wire<1> cache__rr__s7__next{};
  pyc::cpp::Wire<1> pyc_reg_98{};
  pyc::cpp::Wire<1> cache__rr__s7{};
  pyc::cpp::Wire<1> pyc_and_99{};
  pyc::cpp::Wire<1> cache__req_fire__jit_cache__L194{};
  pyc::cpp::Wire<32> cache__addr__jit_cache__L196{};
  pyc::cpp::Wire<3> pyc_extract_100{};
  pyc::cpp::Wire<3> cache__set_idx__jit_cache__L197{};
  pyc::cpp::Wire<27> pyc_extract_101{};
  pyc::cpp::Wire<27> cache__tag__jit_cache__L198{};
  pyc::cpp::Wire<1> pyc_eq_102{};
  pyc::cpp::Wire<1> pyc_mux_103{};
  pyc::cpp::Wire<1> pyc_eq_104{};
  pyc::cpp::Wire<1> pyc_mux_105{};
  pyc::cpp::Wire<1> pyc_eq_106{};
  pyc::cpp::Wire<1> pyc_mux_107{};
  pyc::cpp::Wire<1> pyc_eq_108{};
  pyc::cpp::Wire<1> pyc_mux_109{};
  pyc::cpp::Wire<1> pyc_eq_110{};
  pyc::cpp::Wire<1> pyc_mux_111{};
  pyc::cpp::Wire<1> pyc_eq_112{};
  pyc::cpp::Wire<1> pyc_mux_113{};
  pyc::cpp::Wire<1> pyc_eq_114{};
  pyc::cpp::Wire<1> pyc_mux_115{};
  pyc::cpp::Wire<1> pyc_eq_116{};
  pyc::cpp::Wire<1> pyc_mux_117{};
  pyc::cpp::Wire<27> pyc_mux_118{};
  pyc::cpp::Wire<27> pyc_mux_119{};
  pyc::cpp::Wire<27> pyc_mux_120{};
  pyc::cpp::Wire<27> pyc_mux_121{};
  pyc::cpp::Wire<27> pyc_mux_122{};
  pyc::cpp::Wire<27> pyc_mux_123{};
  pyc::cpp::Wire<27> pyc_mux_124{};
  pyc::cpp::Wire<27> pyc_mux_125{};
  pyc::cpp::Wire<32> pyc_mux_126{};
  pyc::cpp::Wire<32> pyc_mux_127{};
  pyc::cpp::Wire<32> pyc_mux_128{};
  pyc::cpp::Wire<32> pyc_mux_129{};
  pyc::cpp::Wire<32> pyc_mux_130{};
  pyc::cpp::Wire<32> pyc_mux_131{};
  pyc::cpp::Wire<32> pyc_mux_132{};
  pyc::cpp::Wire<32> pyc_mux_133{};
  pyc::cpp::Wire<1> pyc_eq_134{};
  pyc::cpp::Wire<1> pyc_and_135{};
  pyc::cpp::Wire<1> pyc_or_136{};
  pyc::cpp::Wire<32> pyc_mux_137{};
  pyc::cpp::Wire<1> pyc_mux_138{};
  pyc::cpp::Wire<1> pyc_mux_139{};
  pyc::cpp::Wire<1> pyc_mux_140{};
  pyc::cpp::Wire<1> pyc_mux_141{};
  pyc::cpp::Wire<1> pyc_mux_142{};
  pyc::cpp::Wire<1> pyc_mux_143{};
  pyc::cpp::Wire<1> pyc_mux_144{};
  pyc::cpp::Wire<1> pyc_mux_145{};
  pyc::cpp::Wire<27> pyc_mux_146{};
  pyc::cpp::Wire<27> pyc_mux_147{};
  pyc::cpp::Wire<27> pyc_mux_148{};
  pyc::cpp::Wire<27> pyc_mux_149{};
  pyc::cpp::Wire<27> pyc_mux_150{};
  pyc::cpp::Wire<27> pyc_mux_151{};
  pyc::cpp::Wire<27> pyc_mux_152{};
  pyc::cpp::Wire<27> pyc_mux_153{};
  pyc::cpp::Wire<32> pyc_mux_154{};
  pyc::cpp::Wire<32> pyc_mux_155{};
  pyc::cpp::Wire<32> pyc_mux_156{};
  pyc::cpp::Wire<32> pyc_mux_157{};
  pyc::cpp::Wire<32> pyc_mux_158{};
  pyc::cpp::Wire<32> pyc_mux_159{};
  pyc::cpp::Wire<32> pyc_mux_160{};
  pyc::cpp::Wire<32> pyc_mux_161{};
  pyc::cpp::Wire<1> pyc_eq_162{};
  pyc::cpp::Wire<1> pyc_and_163{};
  pyc::cpp::Wire<1> pyc_or_164{};
  pyc::cpp::Wire<32> pyc_mux_165{};
  pyc::cpp::Wire<1> cache__hit__jit_cache__L202{};
  pyc::cpp::Wire<32> cache__hit_data__jit_cache__L203{};
  pyc::cpp::Wire<1> pyc_comb_166{};
  pyc::cpp::Wire<1> pyc_comb_167{};
  pyc::cpp::Wire<32> pyc_comb_168{};
  pyc::cpp::Wire<27> pyc_comb_169{};
  pyc::cpp::Wire<1> pyc_comb_170{};
  pyc::cpp::Wire<1> pyc_comb_171{};
  pyc::cpp::Wire<1> pyc_comb_172{};
  pyc::cpp::Wire<1> pyc_comb_173{};
  pyc::cpp::Wire<1> pyc_comb_174{};
  pyc::cpp::Wire<1> pyc_comb_175{};
  pyc::cpp::Wire<1> pyc_comb_176{};
  pyc::cpp::Wire<1> pyc_comb_177{};
  pyc::cpp::Wire<1> pyc_comb_178{};
  pyc::cpp::Wire<32> pyc_comb_179{};
  pyc::cpp::Wire<32> pyc_byte_mem_180{};
  pyc::cpp::Wire<32> cache__mem_rdata__jit_cache__L206{};
  pyc::cpp::Wire<1> pyc_not_181{};
  pyc::cpp::Wire<1> pyc_and_182{};
  pyc::cpp::Wire<1> cache__miss__jit_cache__L218{};
  pyc::cpp::Wire<1> pyc_mux_183{};
  pyc::cpp::Wire<1> pyc_mux_184{};
  pyc::cpp::Wire<1> pyc_mux_185{};
  pyc::cpp::Wire<1> pyc_mux_186{};
  pyc::cpp::Wire<1> pyc_mux_187{};
  pyc::cpp::Wire<1> pyc_mux_188{};
  pyc::cpp::Wire<1> pyc_mux_189{};
  pyc::cpp::Wire<1> pyc_mux_190{};
  pyc::cpp::Wire<1> cache__repl_way__jit_cache__L219{};
  pyc::cpp::Wire<1> pyc_and_191{};
  pyc::cpp::Wire<1> pyc_add_192{};
  pyc::cpp::Wire<1> pyc_eq_193{};
  pyc::cpp::Wire<1> pyc_mux_194{};
  pyc::cpp::Wire<1> pyc_mux_195{};
  pyc::cpp::Wire<32> pyc_comb_196{};
  pyc::cpp::Wire<1> pyc_comb_197{};
  pyc::cpp::Wire<1> pyc_comb_198{};
  pyc::cpp::Wire<1> pyc_comb_199{};
  pyc::cpp::Wire<1> pyc_comb_200{};
  pyc::cpp::Wire<1> pyc_eq_201{};
  pyc::cpp::Wire<1> pyc_and_202{};
  pyc::cpp::Wire<1> pyc_mux_203{};
  pyc::cpp::Wire<1> pyc_comb_204{};
  pyc::cpp::Wire<1> pyc_comb_205{};
  pyc::cpp::Wire<1> pyc_comb_206{};
  pyc::cpp::Wire<27> pyc_mux_207{};
  pyc::cpp::Wire<32> pyc_mux_208{};
  pyc::cpp::Wire<1> pyc_eq_209{};
  pyc::cpp::Wire<1> pyc_and_210{};
  pyc::cpp::Wire<1> pyc_mux_211{};
  pyc::cpp::Wire<1> pyc_comb_212{};
  pyc::cpp::Wire<1> pyc_comb_213{};
  pyc::cpp::Wire<1> pyc_comb_214{};
  pyc::cpp::Wire<27> pyc_mux_215{};
  pyc::cpp::Wire<32> pyc_mux_216{};
  pyc::cpp::Wire<1> pyc_and_217{};
  pyc::cpp::Wire<1> pyc_add_218{};
  pyc::cpp::Wire<1> pyc_eq_219{};
  pyc::cpp::Wire<1> pyc_mux_220{};
  pyc::cpp::Wire<1> pyc_mux_221{};
  pyc::cpp::Wire<1> pyc_comb_222{};
  pyc::cpp::Wire<1> pyc_comb_223{};
  pyc::cpp::Wire<1> pyc_and_224{};
  pyc::cpp::Wire<1> pyc_mux_225{};
  pyc::cpp::Wire<1> pyc_comb_226{};
  pyc::cpp::Wire<1> pyc_comb_227{};
  pyc::cpp::Wire<27> pyc_mux_228{};
  pyc::cpp::Wire<32> pyc_mux_229{};
  pyc::cpp::Wire<1> pyc_and_230{};
  pyc::cpp::Wire<1> pyc_mux_231{};
  pyc::cpp::Wire<1> pyc_comb_232{};
  pyc::cpp::Wire<1> pyc_comb_233{};
  pyc::cpp::Wire<27> pyc_mux_234{};
  pyc::cpp::Wire<32> pyc_mux_235{};
  pyc::cpp::Wire<1> pyc_and_236{};
  pyc::cpp::Wire<1> pyc_add_237{};
  pyc::cpp::Wire<1> pyc_eq_238{};
  pyc::cpp::Wire<1> pyc_mux_239{};
  pyc::cpp::Wire<1> pyc_mux_240{};
  pyc::cpp::Wire<1> pyc_comb_241{};
  pyc::cpp::Wire<1> pyc_comb_242{};
  pyc::cpp::Wire<1> pyc_and_243{};
  pyc::cpp::Wire<1> pyc_mux_244{};
  pyc::cpp::Wire<1> pyc_comb_245{};
  pyc::cpp::Wire<1> pyc_comb_246{};
  pyc::cpp::Wire<27> pyc_mux_247{};
  pyc::cpp::Wire<32> pyc_mux_248{};
  pyc::cpp::Wire<1> pyc_and_249{};
  pyc::cpp::Wire<1> pyc_mux_250{};
  pyc::cpp::Wire<1> pyc_comb_251{};
  pyc::cpp::Wire<1> pyc_comb_252{};
  pyc::cpp::Wire<27> pyc_mux_253{};
  pyc::cpp::Wire<32> pyc_mux_254{};
  pyc::cpp::Wire<1> pyc_and_255{};
  pyc::cpp::Wire<1> pyc_add_256{};
  pyc::cpp::Wire<1> pyc_eq_257{};
  pyc::cpp::Wire<1> pyc_mux_258{};
  pyc::cpp::Wire<1> pyc_mux_259{};
  pyc::cpp::Wire<1> pyc_comb_260{};
  pyc::cpp::Wire<1> pyc_comb_261{};
  pyc::cpp::Wire<1> pyc_and_262{};
  pyc::cpp::Wire<1> pyc_mux_263{};
  pyc::cpp::Wire<1> pyc_comb_264{};
  pyc::cpp::Wire<1> pyc_comb_265{};
  pyc::cpp::Wire<27> pyc_mux_266{};
  pyc::cpp::Wire<32> pyc_mux_267{};
  pyc::cpp::Wire<1> pyc_and_268{};
  pyc::cpp::Wire<1> pyc_mux_269{};
  pyc::cpp::Wire<1> pyc_comb_270{};
  pyc::cpp::Wire<1> pyc_comb_271{};
  pyc::cpp::Wire<27> pyc_mux_272{};
  pyc::cpp::Wire<32> pyc_mux_273{};
  pyc::cpp::Wire<1> pyc_and_274{};
  pyc::cpp::Wire<1> pyc_add_275{};
  pyc::cpp::Wire<1> pyc_eq_276{};
  pyc::cpp::Wire<1> pyc_mux_277{};
  pyc::cpp::Wire<1> pyc_mux_278{};
  pyc::cpp::Wire<1> pyc_comb_279{};
  pyc::cpp::Wire<1> pyc_comb_280{};
  pyc::cpp::Wire<1> pyc_and_281{};
  pyc::cpp::Wire<1> pyc_mux_282{};
  pyc::cpp::Wire<1> pyc_comb_283{};
  pyc::cpp::Wire<1> pyc_comb_284{};
  pyc::cpp::Wire<27> pyc_mux_285{};
  pyc::cpp::Wire<32> pyc_mux_286{};
  pyc::cpp::Wire<1> pyc_and_287{};
  pyc::cpp::Wire<1> pyc_mux_288{};
  pyc::cpp::Wire<1> pyc_comb_289{};
  pyc::cpp::Wire<1> pyc_comb_290{};
  pyc::cpp::Wire<27> pyc_mux_291{};
  pyc::cpp::Wire<32> pyc_mux_292{};
  pyc::cpp::Wire<1> pyc_and_293{};
  pyc::cpp::Wire<1> pyc_add_294{};
  pyc::cpp::Wire<1> pyc_eq_295{};
  pyc::cpp::Wire<1> pyc_mux_296{};
  pyc::cpp::Wire<1> pyc_mux_297{};
  pyc::cpp::Wire<1> pyc_comb_298{};
  pyc::cpp::Wire<1> pyc_comb_299{};
  pyc::cpp::Wire<1> pyc_and_300{};
  pyc::cpp::Wire<1> pyc_mux_301{};
  pyc::cpp::Wire<1> pyc_comb_302{};
  pyc::cpp::Wire<1> pyc_comb_303{};
  pyc::cpp::Wire<27> pyc_mux_304{};
  pyc::cpp::Wire<32> pyc_mux_305{};
  pyc::cpp::Wire<1> pyc_and_306{};
  pyc::cpp::Wire<1> pyc_mux_307{};
  pyc::cpp::Wire<1> pyc_comb_308{};
  pyc::cpp::Wire<1> pyc_comb_309{};
  pyc::cpp::Wire<27> pyc_mux_310{};
  pyc::cpp::Wire<32> pyc_mux_311{};
  pyc::cpp::Wire<1> pyc_and_312{};
  pyc::cpp::Wire<1> pyc_add_313{};
  pyc::cpp::Wire<1> pyc_eq_314{};
  pyc::cpp::Wire<1> pyc_mux_315{};
  pyc::cpp::Wire<1> pyc_mux_316{};
  pyc::cpp::Wire<1> pyc_comb_317{};
  pyc::cpp::Wire<1> pyc_comb_318{};
  pyc::cpp::Wire<1> pyc_and_319{};
  pyc::cpp::Wire<1> pyc_mux_320{};
  pyc::cpp::Wire<1> pyc_comb_321{};
  pyc::cpp::Wire<1> pyc_comb_322{};
  pyc::cpp::Wire<27> pyc_mux_323{};
  pyc::cpp::Wire<32> pyc_mux_324{};
  pyc::cpp::Wire<1> pyc_and_325{};
  pyc::cpp::Wire<1> pyc_mux_326{};
  pyc::cpp::Wire<1> pyc_comb_327{};
  pyc::cpp::Wire<1> pyc_comb_328{};
  pyc::cpp::Wire<27> pyc_mux_329{};
  pyc::cpp::Wire<32> pyc_mux_330{};
  pyc::cpp::Wire<1> pyc_and_331{};
  pyc::cpp::Wire<1> pyc_add_332{};
  pyc::cpp::Wire<1> pyc_eq_333{};
  pyc::cpp::Wire<1> pyc_mux_334{};
  pyc::cpp::Wire<1> pyc_mux_335{};
  pyc::cpp::Wire<1> pyc_comb_336{};
  pyc::cpp::Wire<1> pyc_comb_337{};
  pyc::cpp::Wire<1> pyc_and_338{};
  pyc::cpp::Wire<1> pyc_mux_339{};
  pyc::cpp::Wire<1> pyc_comb_340{};
  pyc::cpp::Wire<1> pyc_comb_341{};
  pyc::cpp::Wire<27> pyc_mux_342{};
  pyc::cpp::Wire<32> pyc_mux_343{};
  pyc::cpp::Wire<1> pyc_and_344{};
  pyc::cpp::Wire<1> pyc_mux_345{};
  pyc::cpp::Wire<1> pyc_comb_346{};
  pyc::cpp::Wire<1> pyc_comb_347{};
  pyc::cpp::Wire<27> pyc_mux_348{};
  pyc::cpp::Wire<32> pyc_mux_349{};
  pyc::cpp::Wire<32> pyc_mux_350{};
  pyc::cpp::Wire<32> cache__rdata__jit_cache__L232{};
  pyc::cpp::Wire<33> pyc_concat_351{};
  pyc::cpp::Wire<33> cache__rsp_pkt__jit_cache__L233{};
  pyc::cpp::Wire<33> pyc_comb_352{};

  pyc::cpp::pyc_reg<1> pyc_reg_43_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_44_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_45_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_46_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_47_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_48_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_49_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_50_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_51_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_52_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_53_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_54_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_55_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_56_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_57_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_58_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_59_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_60_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_61_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_62_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_63_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_64_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_65_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_66_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_67_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_68_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_69_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_70_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_71_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_72_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_73_inst;
  pyc::cpp::pyc_reg<27> pyc_reg_74_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_75_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_76_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_77_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_78_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_79_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_80_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_81_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_82_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_83_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_84_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_85_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_86_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_87_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_88_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_89_inst;
  pyc::cpp::pyc_reg<32> pyc_reg_90_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_91_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_92_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_93_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_94_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_95_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_96_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_97_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_98_inst;
  pyc::cpp::pyc_fifo<32, 2> pyc_fifo_33_inst;
  pyc::cpp::pyc_fifo<33, 2> pyc_fifo_36_inst;
  pyc::cpp::pyc_byte_mem<32, 32, 4096> main_mem;

  JitCache() :
      pyc_reg_43_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s0__w0__next, pyc_comb_25, pyc_reg_43),
      pyc_reg_44_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s0__w1__next, pyc_comb_25, pyc_reg_44),
      pyc_reg_45_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s1__w0__next, pyc_comb_25, pyc_reg_45),
      pyc_reg_46_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s1__w1__next, pyc_comb_25, pyc_reg_46),
      pyc_reg_47_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s2__w0__next, pyc_comb_25, pyc_reg_47),
      pyc_reg_48_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s2__w1__next, pyc_comb_25, pyc_reg_48),
      pyc_reg_49_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s3__w0__next, pyc_comb_25, pyc_reg_49),
      pyc_reg_50_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s3__w1__next, pyc_comb_25, pyc_reg_50),
      pyc_reg_51_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s4__w0__next, pyc_comb_25, pyc_reg_51),
      pyc_reg_52_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s4__w1__next, pyc_comb_25, pyc_reg_52),
      pyc_reg_53_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s5__w0__next, pyc_comb_25, pyc_reg_53),
      pyc_reg_54_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s5__w1__next, pyc_comb_25, pyc_reg_54),
      pyc_reg_55_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s6__w0__next, pyc_comb_25, pyc_reg_55),
      pyc_reg_56_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s6__w1__next, pyc_comb_25, pyc_reg_56),
      pyc_reg_57_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s7__w0__next, pyc_comb_25, pyc_reg_57),
      pyc_reg_58_inst(sys_clk, sys_rst, pyc_comb_23, cache__valid__s7__w1__next, pyc_comb_25, pyc_reg_58),
      pyc_reg_59_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s0__w0__next, pyc_comb_22, pyc_reg_59),
      pyc_reg_60_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s0__w1__next, pyc_comb_22, pyc_reg_60),
      pyc_reg_61_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s1__w0__next, pyc_comb_22, pyc_reg_61),
      pyc_reg_62_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s1__w1__next, pyc_comb_22, pyc_reg_62),
      pyc_reg_63_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s2__w0__next, pyc_comb_22, pyc_reg_63),
      pyc_reg_64_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s2__w1__next, pyc_comb_22, pyc_reg_64),
      pyc_reg_65_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s3__w0__next, pyc_comb_22, pyc_reg_65),
      pyc_reg_66_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s3__w1__next, pyc_comb_22, pyc_reg_66),
      pyc_reg_67_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s4__w0__next, pyc_comb_22, pyc_reg_67),
      pyc_reg_68_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s4__w1__next, pyc_comb_22, pyc_reg_68),
      pyc_reg_69_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s5__w0__next, pyc_comb_22, pyc_reg_69),
      pyc_reg_70_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s5__w1__next, pyc_comb_22, pyc_reg_70),
      pyc_reg_71_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s6__w0__next, pyc_comb_22, pyc_reg_71),
      pyc_reg_72_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s6__w1__next, pyc_comb_22, pyc_reg_72),
      pyc_reg_73_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s7__w0__next, pyc_comb_22, pyc_reg_73),
      pyc_reg_74_inst(sys_clk, sys_rst, pyc_comb_23, cache__tag__s7__w1__next, pyc_comb_22, pyc_reg_74),
      pyc_reg_75_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s0__w0__next, pyc_comb_24, pyc_reg_75),
      pyc_reg_76_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s0__w1__next, pyc_comb_24, pyc_reg_76),
      pyc_reg_77_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s1__w0__next, pyc_comb_24, pyc_reg_77),
      pyc_reg_78_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s1__w1__next, pyc_comb_24, pyc_reg_78),
      pyc_reg_79_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s2__w0__next, pyc_comb_24, pyc_reg_79),
      pyc_reg_80_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s2__w1__next, pyc_comb_24, pyc_reg_80),
      pyc_reg_81_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s3__w0__next, pyc_comb_24, pyc_reg_81),
      pyc_reg_82_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s3__w1__next, pyc_comb_24, pyc_reg_82),
      pyc_reg_83_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s4__w0__next, pyc_comb_24, pyc_reg_83),
      pyc_reg_84_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s4__w1__next, pyc_comb_24, pyc_reg_84),
      pyc_reg_85_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s5__w0__next, pyc_comb_24, pyc_reg_85),
      pyc_reg_86_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s5__w1__next, pyc_comb_24, pyc_reg_86),
      pyc_reg_87_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s6__w0__next, pyc_comb_24, pyc_reg_87),
      pyc_reg_88_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s6__w1__next, pyc_comb_24, pyc_reg_88),
      pyc_reg_89_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s7__w0__next, pyc_comb_24, pyc_reg_89),
      pyc_reg_90_inst(sys_clk, sys_rst, pyc_comb_23, cache__data__s7__w1__next, pyc_comb_24, pyc_reg_90),
      pyc_reg_91_inst(sys_clk, sys_rst, pyc_comb_23, cache__rr__s0__next, pyc_comb_25, pyc_reg_91),
      pyc_reg_92_inst(sys_clk, sys_rst, pyc_comb_23, cache__rr__s1__next, pyc_comb_25, pyc_reg_92),
      pyc_reg_93_inst(sys_clk, sys_rst, pyc_comb_23, cache__rr__s2__next, pyc_comb_25, pyc_reg_93),
      pyc_reg_94_inst(sys_clk, sys_rst, pyc_comb_23, cache__rr__s3__next, pyc_comb_25, pyc_reg_94),
      pyc_reg_95_inst(sys_clk, sys_rst, pyc_comb_23, cache__rr__s4__next, pyc_comb_25, pyc_reg_95),
      pyc_reg_96_inst(sys_clk, sys_rst, pyc_comb_23, cache__rr__s5__next, pyc_comb_25, pyc_reg_96),
      pyc_reg_97_inst(sys_clk, sys_rst, pyc_comb_23, cache__rr__s6__next, pyc_comb_25, pyc_reg_97),
      pyc_reg_98_inst(sys_clk, sys_rst, pyc_comb_23, cache__rr__s7__next, pyc_comb_25, pyc_reg_98),
      pyc_fifo_33_inst(sys_clk, sys_rst, cache__req_q__in_valid, pyc_fifo_33, cache__req_q__in_data, pyc_fifo_34, cache__req_q__out_ready, pyc_fifo_35),
      pyc_fifo_36_inst(sys_clk, sys_rst, cache__rsp_q__in_valid, pyc_fifo_36, cache__rsp_q__in_data, pyc_fifo_37, cache__rsp_q__out_ready, pyc_fifo_38),
      main_mem(sys_clk, sys_rst, pyc_comb_168, pyc_byte_mem_180, pyc_comb_29, pyc_comb_30, pyc_comb_31, pyc_comb_32) {
    eval();
  }

  inline void eval_comb_0() {
    pyc_constant_1 = pyc::cpp::Wire<3>(7ull);
    pyc_constant_2 = pyc::cpp::Wire<3>(6ull);
    pyc_constant_3 = pyc::cpp::Wire<3>(5ull);
    pyc_constant_4 = pyc::cpp::Wire<3>(4ull);
    pyc_constant_5 = pyc::cpp::Wire<3>(3ull);
    pyc_constant_6 = pyc::cpp::Wire<3>(2ull);
    pyc_constant_7 = pyc::cpp::Wire<3>(1ull);
    pyc_constant_8 = pyc::cpp::Wire<3>(0ull);
    pyc_constant_9 = pyc::cpp::Wire<27>(0ull);
    pyc_constant_10 = pyc::cpp::Wire<1>(1ull);
    pyc_constant_11 = pyc::cpp::Wire<4>(0ull);
    pyc_constant_12 = pyc::cpp::Wire<32>(0ull);
    pyc_constant_13 = pyc::cpp::Wire<1>(0ull);
    req_valid__jit_cache__L166 = req_valid;
    req_addr__jit_cache__L167 = req_addr;
    rsp_ready__jit_cache__L168 = rsp_ready;
    cache__wvalid0__jit_cache__L172 = pyc_constant_13;
    cache__waddr0__jit_cache__L173 = pyc_constant_12;
    cache__wdata0__jit_cache__L174 = pyc_constant_12;
    cache__wstrb0__jit_cache__L175 = pyc_constant_11;
    pyc_comb_14 = pyc_constant_1;
    pyc_comb_15 = pyc_constant_2;
    pyc_comb_16 = pyc_constant_3;
    pyc_comb_17 = pyc_constant_4;
    pyc_comb_18 = pyc_constant_5;
    pyc_comb_19 = pyc_constant_6;
    pyc_comb_20 = pyc_constant_7;
    pyc_comb_21 = pyc_constant_8;
    pyc_comb_22 = pyc_constant_9;
    pyc_comb_23 = pyc_constant_10;
    pyc_comb_24 = pyc_constant_12;
    pyc_comb_25 = pyc_constant_13;
    pyc_comb_26 = req_valid__jit_cache__L166;
    pyc_comb_27 = req_addr__jit_cache__L167;
    pyc_comb_28 = rsp_ready__jit_cache__L168;
    pyc_comb_29 = cache__wvalid0__jit_cache__L172;
    pyc_comb_30 = cache__waddr0__jit_cache__L173;
    pyc_comb_31 = cache__wdata0__jit_cache__L174;
    pyc_comb_32 = cache__wstrb0__jit_cache__L175;
  }

  inline void eval_comb_1() {
    pyc_extract_39 = pyc::cpp::extract<1, 33>(pyc_fifo_38, 32u);
    cache__rsp_hit__jit_cache__L183 = pyc_extract_39;
    pyc_extract_40 = pyc::cpp::extract<32, 33>(pyc_fifo_38, 0u);
    cache__rsp_rdata__jit_cache__L184 = pyc_extract_40;
    pyc_comb_41 = cache__rsp_hit__jit_cache__L183;
    pyc_comb_42 = cache__rsp_rdata__jit_cache__L184;
  }

  inline void eval_comb_2() {
    cache__rr__s7 = pyc_reg_98;
    pyc_and_99 = (pyc_fifo_34 & pyc_fifo_36);
    cache__req_fire__jit_cache__L194 = pyc_and_99;
    cache__addr__jit_cache__L196 = pyc_fifo_35;
    pyc_extract_100 = pyc::cpp::extract<3, 32>(cache__addr__jit_cache__L196, 2u);
    cache__set_idx__jit_cache__L197 = pyc_extract_100;
    pyc_extract_101 = pyc::cpp::extract<27, 32>(cache__addr__jit_cache__L196, 5u);
    cache__tag__jit_cache__L198 = pyc_extract_101;
    pyc_eq_102 = pyc::cpp::Wire<1>((cache__set_idx__jit_cache__L197 == pyc_comb_21) ? 1u : 0u);
    pyc_mux_103 = (pyc_eq_102.toBool() ? cache__valid__s0__w0 : pyc_comb_25);
    pyc_eq_104 = pyc::cpp::Wire<1>((cache__set_idx__jit_cache__L197 == pyc_comb_20) ? 1u : 0u);
    pyc_mux_105 = (pyc_eq_104.toBool() ? cache__valid__s1__w0 : pyc_mux_103);
    pyc_eq_106 = pyc::cpp::Wire<1>((cache__set_idx__jit_cache__L197 == pyc_comb_19) ? 1u : 0u);
    pyc_mux_107 = (pyc_eq_106.toBool() ? cache__valid__s2__w0 : pyc_mux_105);
    pyc_eq_108 = pyc::cpp::Wire<1>((cache__set_idx__jit_cache__L197 == pyc_comb_18) ? 1u : 0u);
    pyc_mux_109 = (pyc_eq_108.toBool() ? cache__valid__s3__w0 : pyc_mux_107);
    pyc_eq_110 = pyc::cpp::Wire<1>((cache__set_idx__jit_cache__L197 == pyc_comb_17) ? 1u : 0u);
    pyc_mux_111 = (pyc_eq_110.toBool() ? cache__valid__s4__w0 : pyc_mux_109);
    pyc_eq_112 = pyc::cpp::Wire<1>((cache__set_idx__jit_cache__L197 == pyc_comb_16) ? 1u : 0u);
    pyc_mux_113 = (pyc_eq_112.toBool() ? cache__valid__s5__w0 : pyc_mux_111);
    pyc_eq_114 = pyc::cpp::Wire<1>((cache__set_idx__jit_cache__L197 == pyc_comb_15) ? 1u : 0u);
    pyc_mux_115 = (pyc_eq_114.toBool() ? cache__valid__s6__w0 : pyc_mux_113);
    pyc_eq_116 = pyc::cpp::Wire<1>((cache__set_idx__jit_cache__L197 == pyc_comb_14) ? 1u : 0u);
    pyc_mux_117 = (pyc_eq_116.toBool() ? cache__valid__s7__w0 : pyc_mux_115);
    pyc_mux_118 = (pyc_eq_102.toBool() ? cache__tag__s0__w0 : pyc_comb_22);
    pyc_mux_119 = (pyc_eq_104.toBool() ? cache__tag__s1__w0 : pyc_mux_118);
    pyc_mux_120 = (pyc_eq_106.toBool() ? cache__tag__s2__w0 : pyc_mux_119);
    pyc_mux_121 = (pyc_eq_108.toBool() ? cache__tag__s3__w0 : pyc_mux_120);
    pyc_mux_122 = (pyc_eq_110.toBool() ? cache__tag__s4__w0 : pyc_mux_121);
    pyc_mux_123 = (pyc_eq_112.toBool() ? cache__tag__s5__w0 : pyc_mux_122);
    pyc_mux_124 = (pyc_eq_114.toBool() ? cache__tag__s6__w0 : pyc_mux_123);
    pyc_mux_125 = (pyc_eq_116.toBool() ? cache__tag__s7__w0 : pyc_mux_124);
    pyc_mux_126 = (pyc_eq_102.toBool() ? cache__data__s0__w0 : pyc_comb_24);
    pyc_mux_127 = (pyc_eq_104.toBool() ? cache__data__s1__w0 : pyc_mux_126);
    pyc_mux_128 = (pyc_eq_106.toBool() ? cache__data__s2__w0 : pyc_mux_127);
    pyc_mux_129 = (pyc_eq_108.toBool() ? cache__data__s3__w0 : pyc_mux_128);
    pyc_mux_130 = (pyc_eq_110.toBool() ? cache__data__s4__w0 : pyc_mux_129);
    pyc_mux_131 = (pyc_eq_112.toBool() ? cache__data__s5__w0 : pyc_mux_130);
    pyc_mux_132 = (pyc_eq_114.toBool() ? cache__data__s6__w0 : pyc_mux_131);
    pyc_mux_133 = (pyc_eq_116.toBool() ? cache__data__s7__w0 : pyc_mux_132);
    pyc_eq_134 = pyc::cpp::Wire<1>((pyc_mux_125 == cache__tag__jit_cache__L198) ? 1u : 0u);
    pyc_and_135 = (pyc_mux_117 & pyc_eq_134);
    pyc_or_136 = (pyc_comb_25 | pyc_and_135);
    pyc_mux_137 = (pyc_and_135.toBool() ? pyc_mux_133 : pyc_comb_24);
    pyc_mux_138 = (pyc_eq_102.toBool() ? cache__valid__s0__w1 : pyc_comb_25);
    pyc_mux_139 = (pyc_eq_104.toBool() ? cache__valid__s1__w1 : pyc_mux_138);
    pyc_mux_140 = (pyc_eq_106.toBool() ? cache__valid__s2__w1 : pyc_mux_139);
    pyc_mux_141 = (pyc_eq_108.toBool() ? cache__valid__s3__w1 : pyc_mux_140);
    pyc_mux_142 = (pyc_eq_110.toBool() ? cache__valid__s4__w1 : pyc_mux_141);
    pyc_mux_143 = (pyc_eq_112.toBool() ? cache__valid__s5__w1 : pyc_mux_142);
    pyc_mux_144 = (pyc_eq_114.toBool() ? cache__valid__s6__w1 : pyc_mux_143);
    pyc_mux_145 = (pyc_eq_116.toBool() ? cache__valid__s7__w1 : pyc_mux_144);
    pyc_mux_146 = (pyc_eq_102.toBool() ? cache__tag__s0__w1 : pyc_comb_22);
    pyc_mux_147 = (pyc_eq_104.toBool() ? cache__tag__s1__w1 : pyc_mux_146);
    pyc_mux_148 = (pyc_eq_106.toBool() ? cache__tag__s2__w1 : pyc_mux_147);
    pyc_mux_149 = (pyc_eq_108.toBool() ? cache__tag__s3__w1 : pyc_mux_148);
    pyc_mux_150 = (pyc_eq_110.toBool() ? cache__tag__s4__w1 : pyc_mux_149);
    pyc_mux_151 = (pyc_eq_112.toBool() ? cache__tag__s5__w1 : pyc_mux_150);
    pyc_mux_152 = (pyc_eq_114.toBool() ? cache__tag__s6__w1 : pyc_mux_151);
    pyc_mux_153 = (pyc_eq_116.toBool() ? cache__tag__s7__w1 : pyc_mux_152);
    pyc_mux_154 = (pyc_eq_102.toBool() ? cache__data__s0__w1 : pyc_comb_24);
    pyc_mux_155 = (pyc_eq_104.toBool() ? cache__data__s1__w1 : pyc_mux_154);
    pyc_mux_156 = (pyc_eq_106.toBool() ? cache__data__s2__w1 : pyc_mux_155);
    pyc_mux_157 = (pyc_eq_108.toBool() ? cache__data__s3__w1 : pyc_mux_156);
    pyc_mux_158 = (pyc_eq_110.toBool() ? cache__data__s4__w1 : pyc_mux_157);
    pyc_mux_159 = (pyc_eq_112.toBool() ? cache__data__s5__w1 : pyc_mux_158);
    pyc_mux_160 = (pyc_eq_114.toBool() ? cache__data__s6__w1 : pyc_mux_159);
    pyc_mux_161 = (pyc_eq_116.toBool() ? cache__data__s7__w1 : pyc_mux_160);
    pyc_eq_162 = pyc::cpp::Wire<1>((pyc_mux_153 == cache__tag__jit_cache__L198) ? 1u : 0u);
    pyc_and_163 = (pyc_mux_145 & pyc_eq_162);
    pyc_or_164 = (pyc_or_136 | pyc_and_163);
    pyc_mux_165 = (pyc_and_163.toBool() ? pyc_mux_161 : pyc_mux_137);
    cache__hit__jit_cache__L202 = pyc_or_164;
    cache__hit_data__jit_cache__L203 = pyc_mux_165;
    pyc_comb_166 = cache__rr__s7;
    pyc_comb_167 = cache__req_fire__jit_cache__L194;
    pyc_comb_168 = cache__addr__jit_cache__L196;
    pyc_comb_169 = cache__tag__jit_cache__L198;
    pyc_comb_170 = pyc_eq_102;
    pyc_comb_171 = pyc_eq_104;
    pyc_comb_172 = pyc_eq_106;
    pyc_comb_173 = pyc_eq_108;
    pyc_comb_174 = pyc_eq_110;
    pyc_comb_175 = pyc_eq_112;
    pyc_comb_176 = pyc_eq_114;
    pyc_comb_177 = pyc_eq_116;
    pyc_comb_178 = cache__hit__jit_cache__L202;
    pyc_comb_179 = cache__hit_data__jit_cache__L203;
  }

  inline void eval_comb_3() {
    cache__mem_rdata__jit_cache__L206 = pyc_byte_mem_180;
    pyc_not_181 = (~pyc_comb_178);
    pyc_and_182 = (pyc_comb_167 & pyc_not_181);
    cache__miss__jit_cache__L218 = pyc_and_182;
    pyc_mux_183 = (pyc_comb_170.toBool() ? cache__rr__s0 : pyc_comb_25);
    pyc_mux_184 = (pyc_comb_171.toBool() ? cache__rr__s1 : pyc_mux_183);
    pyc_mux_185 = (pyc_comb_172.toBool() ? cache__rr__s2 : pyc_mux_184);
    pyc_mux_186 = (pyc_comb_173.toBool() ? cache__rr__s3 : pyc_mux_185);
    pyc_mux_187 = (pyc_comb_174.toBool() ? cache__rr__s4 : pyc_mux_186);
    pyc_mux_188 = (pyc_comb_175.toBool() ? cache__rr__s5 : pyc_mux_187);
    pyc_mux_189 = (pyc_comb_176.toBool() ? cache__rr__s6 : pyc_mux_188);
    pyc_mux_190 = (pyc_comb_177.toBool() ? pyc_comb_166 : pyc_mux_189);
    cache__repl_way__jit_cache__L219 = pyc_mux_190;
    pyc_and_191 = (cache__miss__jit_cache__L218 & pyc_comb_170);
    pyc_add_192 = (cache__rr__s0 + pyc_comb_23);
    pyc_eq_193 = pyc::cpp::Wire<1>((cache__rr__s0 == pyc_comb_23) ? 1u : 0u);
    pyc_mux_194 = (pyc_eq_193.toBool() ? pyc_comb_25 : pyc_add_192);
    pyc_mux_195 = (pyc_and_191.toBool() ? pyc_mux_194 : cache__rr__s0);
    pyc_comb_196 = cache__mem_rdata__jit_cache__L206;
    pyc_comb_197 = cache__miss__jit_cache__L218;
    pyc_comb_198 = cache__repl_way__jit_cache__L219;
    pyc_comb_199 = pyc_and_191;
    pyc_comb_200 = pyc_mux_195;
  }

  inline void eval_comb_4() {
    pyc_eq_201 = pyc::cpp::Wire<1>((pyc_comb_198 == pyc_comb_25) ? 1u : 0u);
    pyc_and_202 = (pyc_comb_199 & pyc_eq_201);
    pyc_mux_203 = (pyc_and_202.toBool() ? pyc_comb_23 : cache__valid__s0__w0);
    pyc_comb_204 = pyc_eq_201;
    pyc_comb_205 = pyc_and_202;
    pyc_comb_206 = pyc_mux_203;
  }

  inline void eval_comb_5() {
    pyc_eq_209 = pyc::cpp::Wire<1>((pyc_comb_198 == pyc_comb_23) ? 1u : 0u);
    pyc_and_210 = (pyc_comb_199 & pyc_eq_209);
    pyc_mux_211 = (pyc_and_210.toBool() ? pyc_comb_23 : cache__valid__s0__w1);
    pyc_comb_212 = pyc_eq_209;
    pyc_comb_213 = pyc_and_210;
    pyc_comb_214 = pyc_mux_211;
  }

  inline void eval_comb_6() {
    pyc_and_217 = (pyc_comb_197 & pyc_comb_171);
    pyc_add_218 = (cache__rr__s1 + pyc_comb_23);
    pyc_eq_219 = pyc::cpp::Wire<1>((cache__rr__s1 == pyc_comb_23) ? 1u : 0u);
    pyc_mux_220 = (pyc_eq_219.toBool() ? pyc_comb_25 : pyc_add_218);
    pyc_mux_221 = (pyc_and_217.toBool() ? pyc_mux_220 : cache__rr__s1);
    pyc_comb_222 = pyc_and_217;
    pyc_comb_223 = pyc_mux_221;
  }

  inline void eval_comb_7() {
    pyc_and_224 = (pyc_comb_222 & pyc_comb_204);
    pyc_mux_225 = (pyc_and_224.toBool() ? pyc_comb_23 : cache__valid__s1__w0);
    pyc_comb_226 = pyc_and_224;
    pyc_comb_227 = pyc_mux_225;
  }

  inline void eval_comb_8() {
    pyc_and_230 = (pyc_comb_222 & pyc_comb_212);
    pyc_mux_231 = (pyc_and_230.toBool() ? pyc_comb_23 : cache__valid__s1__w1);
    pyc_comb_232 = pyc_and_230;
    pyc_comb_233 = pyc_mux_231;
  }

  inline void eval_comb_9() {
    pyc_and_236 = (pyc_comb_197 & pyc_comb_172);
    pyc_add_237 = (cache__rr__s2 + pyc_comb_23);
    pyc_eq_238 = pyc::cpp::Wire<1>((cache__rr__s2 == pyc_comb_23) ? 1u : 0u);
    pyc_mux_239 = (pyc_eq_238.toBool() ? pyc_comb_25 : pyc_add_237);
    pyc_mux_240 = (pyc_and_236.toBool() ? pyc_mux_239 : cache__rr__s2);
    pyc_comb_241 = pyc_and_236;
    pyc_comb_242 = pyc_mux_240;
  }

  inline void eval_comb_10() {
    pyc_and_243 = (pyc_comb_241 & pyc_comb_204);
    pyc_mux_244 = (pyc_and_243.toBool() ? pyc_comb_23 : cache__valid__s2__w0);
    pyc_comb_245 = pyc_and_243;
    pyc_comb_246 = pyc_mux_244;
  }

  inline void eval_comb_11() {
    pyc_and_249 = (pyc_comb_241 & pyc_comb_212);
    pyc_mux_250 = (pyc_and_249.toBool() ? pyc_comb_23 : cache__valid__s2__w1);
    pyc_comb_251 = pyc_and_249;
    pyc_comb_252 = pyc_mux_250;
  }

  inline void eval_comb_12() {
    pyc_and_255 = (pyc_comb_197 & pyc_comb_173);
    pyc_add_256 = (cache__rr__s3 + pyc_comb_23);
    pyc_eq_257 = pyc::cpp::Wire<1>((cache__rr__s3 == pyc_comb_23) ? 1u : 0u);
    pyc_mux_258 = (pyc_eq_257.toBool() ? pyc_comb_25 : pyc_add_256);
    pyc_mux_259 = (pyc_and_255.toBool() ? pyc_mux_258 : cache__rr__s3);
    pyc_comb_260 = pyc_and_255;
    pyc_comb_261 = pyc_mux_259;
  }

  inline void eval_comb_13() {
    pyc_and_262 = (pyc_comb_260 & pyc_comb_204);
    pyc_mux_263 = (pyc_and_262.toBool() ? pyc_comb_23 : cache__valid__s3__w0);
    pyc_comb_264 = pyc_and_262;
    pyc_comb_265 = pyc_mux_263;
  }

  inline void eval_comb_14() {
    pyc_and_268 = (pyc_comb_260 & pyc_comb_212);
    pyc_mux_269 = (pyc_and_268.toBool() ? pyc_comb_23 : cache__valid__s3__w1);
    pyc_comb_270 = pyc_and_268;
    pyc_comb_271 = pyc_mux_269;
  }

  inline void eval_comb_15() {
    pyc_and_274 = (pyc_comb_197 & pyc_comb_174);
    pyc_add_275 = (cache__rr__s4 + pyc_comb_23);
    pyc_eq_276 = pyc::cpp::Wire<1>((cache__rr__s4 == pyc_comb_23) ? 1u : 0u);
    pyc_mux_277 = (pyc_eq_276.toBool() ? pyc_comb_25 : pyc_add_275);
    pyc_mux_278 = (pyc_and_274.toBool() ? pyc_mux_277 : cache__rr__s4);
    pyc_comb_279 = pyc_and_274;
    pyc_comb_280 = pyc_mux_278;
  }

  inline void eval_comb_16() {
    pyc_and_281 = (pyc_comb_279 & pyc_comb_204);
    pyc_mux_282 = (pyc_and_281.toBool() ? pyc_comb_23 : cache__valid__s4__w0);
    pyc_comb_283 = pyc_and_281;
    pyc_comb_284 = pyc_mux_282;
  }

  inline void eval_comb_17() {
    pyc_and_287 = (pyc_comb_279 & pyc_comb_212);
    pyc_mux_288 = (pyc_and_287.toBool() ? pyc_comb_23 : cache__valid__s4__w1);
    pyc_comb_289 = pyc_and_287;
    pyc_comb_290 = pyc_mux_288;
  }

  inline void eval_comb_18() {
    pyc_and_293 = (pyc_comb_197 & pyc_comb_175);
    pyc_add_294 = (cache__rr__s5 + pyc_comb_23);
    pyc_eq_295 = pyc::cpp::Wire<1>((cache__rr__s5 == pyc_comb_23) ? 1u : 0u);
    pyc_mux_296 = (pyc_eq_295.toBool() ? pyc_comb_25 : pyc_add_294);
    pyc_mux_297 = (pyc_and_293.toBool() ? pyc_mux_296 : cache__rr__s5);
    pyc_comb_298 = pyc_and_293;
    pyc_comb_299 = pyc_mux_297;
  }

  inline void eval_comb_19() {
    pyc_and_300 = (pyc_comb_298 & pyc_comb_204);
    pyc_mux_301 = (pyc_and_300.toBool() ? pyc_comb_23 : cache__valid__s5__w0);
    pyc_comb_302 = pyc_and_300;
    pyc_comb_303 = pyc_mux_301;
  }

  inline void eval_comb_20() {
    pyc_and_306 = (pyc_comb_298 & pyc_comb_212);
    pyc_mux_307 = (pyc_and_306.toBool() ? pyc_comb_23 : cache__valid__s5__w1);
    pyc_comb_308 = pyc_and_306;
    pyc_comb_309 = pyc_mux_307;
  }

  inline void eval_comb_21() {
    pyc_and_312 = (pyc_comb_197 & pyc_comb_176);
    pyc_add_313 = (cache__rr__s6 + pyc_comb_23);
    pyc_eq_314 = pyc::cpp::Wire<1>((cache__rr__s6 == pyc_comb_23) ? 1u : 0u);
    pyc_mux_315 = (pyc_eq_314.toBool() ? pyc_comb_25 : pyc_add_313);
    pyc_mux_316 = (pyc_and_312.toBool() ? pyc_mux_315 : cache__rr__s6);
    pyc_comb_317 = pyc_and_312;
    pyc_comb_318 = pyc_mux_316;
  }

  inline void eval_comb_22() {
    pyc_and_319 = (pyc_comb_317 & pyc_comb_204);
    pyc_mux_320 = (pyc_and_319.toBool() ? pyc_comb_23 : cache__valid__s6__w0);
    pyc_comb_321 = pyc_and_319;
    pyc_comb_322 = pyc_mux_320;
  }

  inline void eval_comb_23() {
    pyc_and_325 = (pyc_comb_317 & pyc_comb_212);
    pyc_mux_326 = (pyc_and_325.toBool() ? pyc_comb_23 : cache__valid__s6__w1);
    pyc_comb_327 = pyc_and_325;
    pyc_comb_328 = pyc_mux_326;
  }

  inline void eval_comb_24() {
    pyc_and_331 = (pyc_comb_197 & pyc_comb_177);
    pyc_add_332 = (pyc_comb_166 + pyc_comb_23);
    pyc_eq_333 = pyc::cpp::Wire<1>((pyc_comb_166 == pyc_comb_23) ? 1u : 0u);
    pyc_mux_334 = (pyc_eq_333.toBool() ? pyc_comb_25 : pyc_add_332);
    pyc_mux_335 = (pyc_and_331.toBool() ? pyc_mux_334 : pyc_comb_166);
    pyc_comb_336 = pyc_and_331;
    pyc_comb_337 = pyc_mux_335;
  }

  inline void eval_comb_25() {
    pyc_and_338 = (pyc_comb_336 & pyc_comb_204);
    pyc_mux_339 = (pyc_and_338.toBool() ? pyc_comb_23 : cache__valid__s7__w0);
    pyc_comb_340 = pyc_and_338;
    pyc_comb_341 = pyc_mux_339;
  }

  inline void eval_comb_26() {
    pyc_and_344 = (pyc_comb_336 & pyc_comb_212);
    pyc_mux_345 = (pyc_and_344.toBool() ? pyc_comb_23 : cache__valid__s7__w1);
    pyc_comb_346 = pyc_and_344;
    pyc_comb_347 = pyc_mux_345;
  }

  inline void eval_comb_27() {
    pyc_mux_350 = (pyc_comb_178.toBool() ? pyc_comb_179 : pyc_comb_196);
    cache__rdata__jit_cache__L232 = pyc_mux_350;
    pyc_concat_351 = pyc::cpp::concat(pyc_comb_178, cache__rdata__jit_cache__L232);
    cache__rsp_pkt__jit_cache__L233 = pyc_concat_351;
    pyc_comb_352 = cache__rsp_pkt__jit_cache__L233;
  }

  inline void eval_comb_pass() {
    eval_comb_0();
    eval_comb_1();
    cache__valid__s0__w0 = pyc_reg_43;
    cache__valid__s0__w1 = pyc_reg_44;
    cache__valid__s1__w0 = pyc_reg_45;
    cache__valid__s1__w1 = pyc_reg_46;
    cache__valid__s2__w0 = pyc_reg_47;
    cache__valid__s2__w1 = pyc_reg_48;
    cache__valid__s3__w0 = pyc_reg_49;
    cache__valid__s3__w1 = pyc_reg_50;
    cache__valid__s4__w0 = pyc_reg_51;
    cache__valid__s4__w1 = pyc_reg_52;
    cache__valid__s5__w0 = pyc_reg_53;
    cache__valid__s5__w1 = pyc_reg_54;
    cache__valid__s6__w0 = pyc_reg_55;
    cache__valid__s6__w1 = pyc_reg_56;
    cache__valid__s7__w0 = pyc_reg_57;
    cache__valid__s7__w1 = pyc_reg_58;
    cache__tag__s0__w0 = pyc_reg_59;
    cache__tag__s0__w1 = pyc_reg_60;
    cache__tag__s1__w0 = pyc_reg_61;
    cache__tag__s1__w1 = pyc_reg_62;
    cache__tag__s2__w0 = pyc_reg_63;
    cache__tag__s2__w1 = pyc_reg_64;
    cache__tag__s3__w0 = pyc_reg_65;
    cache__tag__s3__w1 = pyc_reg_66;
    cache__tag__s4__w0 = pyc_reg_67;
    cache__tag__s4__w1 = pyc_reg_68;
    cache__tag__s5__w0 = pyc_reg_69;
    cache__tag__s5__w1 = pyc_reg_70;
    cache__tag__s6__w0 = pyc_reg_71;
    cache__tag__s6__w1 = pyc_reg_72;
    cache__tag__s7__w0 = pyc_reg_73;
    cache__tag__s7__w1 = pyc_reg_74;
    cache__data__s0__w0 = pyc_reg_75;
    cache__data__s0__w1 = pyc_reg_76;
    cache__data__s1__w0 = pyc_reg_77;
    cache__data__s1__w1 = pyc_reg_78;
    cache__data__s2__w0 = pyc_reg_79;
    cache__data__s2__w1 = pyc_reg_80;
    cache__data__s3__w0 = pyc_reg_81;
    cache__data__s3__w1 = pyc_reg_82;
    cache__data__s4__w0 = pyc_reg_83;
    cache__data__s4__w1 = pyc_reg_84;
    cache__data__s5__w0 = pyc_reg_85;
    cache__data__s5__w1 = pyc_reg_86;
    cache__data__s6__w0 = pyc_reg_87;
    cache__data__s6__w1 = pyc_reg_88;
    cache__data__s7__w0 = pyc_reg_89;
    cache__data__s7__w1 = pyc_reg_90;
    cache__rr__s0 = pyc_reg_91;
    cache__rr__s1 = pyc_reg_92;
    cache__rr__s2 = pyc_reg_93;
    cache__rr__s3 = pyc_reg_94;
    cache__rr__s4 = pyc_reg_95;
    cache__rr__s5 = pyc_reg_96;
    cache__rr__s6 = pyc_reg_97;
    eval_comb_2();
    eval_comb_3();
    cache__rr__s0__next = pyc_comb_200;
    eval_comb_4();
    cache__valid__s0__w0__next = pyc_comb_206;
    pyc_mux_207 = (pyc_comb_205.toBool() ? pyc_comb_169 : cache__tag__s0__w0);
    cache__tag__s0__w0__next = pyc_mux_207;
    pyc_mux_208 = (pyc_comb_205.toBool() ? pyc_comb_196 : cache__data__s0__w0);
    cache__data__s0__w0__next = pyc_mux_208;
    eval_comb_5();
    cache__valid__s0__w1__next = pyc_comb_214;
    pyc_mux_215 = (pyc_comb_213.toBool() ? pyc_comb_169 : cache__tag__s0__w1);
    cache__tag__s0__w1__next = pyc_mux_215;
    pyc_mux_216 = (pyc_comb_213.toBool() ? pyc_comb_196 : cache__data__s0__w1);
    cache__data__s0__w1__next = pyc_mux_216;
    eval_comb_6();
    cache__rr__s1__next = pyc_comb_223;
    eval_comb_7();
    cache__valid__s1__w0__next = pyc_comb_227;
    pyc_mux_228 = (pyc_comb_226.toBool() ? pyc_comb_169 : cache__tag__s1__w0);
    cache__tag__s1__w0__next = pyc_mux_228;
    pyc_mux_229 = (pyc_comb_226.toBool() ? pyc_comb_196 : cache__data__s1__w0);
    cache__data__s1__w0__next = pyc_mux_229;
    eval_comb_8();
    cache__valid__s1__w1__next = pyc_comb_233;
    pyc_mux_234 = (pyc_comb_232.toBool() ? pyc_comb_169 : cache__tag__s1__w1);
    cache__tag__s1__w1__next = pyc_mux_234;
    pyc_mux_235 = (pyc_comb_232.toBool() ? pyc_comb_196 : cache__data__s1__w1);
    cache__data__s1__w1__next = pyc_mux_235;
    eval_comb_9();
    cache__rr__s2__next = pyc_comb_242;
    eval_comb_10();
    cache__valid__s2__w0__next = pyc_comb_246;
    pyc_mux_247 = (pyc_comb_245.toBool() ? pyc_comb_169 : cache__tag__s2__w0);
    cache__tag__s2__w0__next = pyc_mux_247;
    pyc_mux_248 = (pyc_comb_245.toBool() ? pyc_comb_196 : cache__data__s2__w0);
    cache__data__s2__w0__next = pyc_mux_248;
    eval_comb_11();
    cache__valid__s2__w1__next = pyc_comb_252;
    pyc_mux_253 = (pyc_comb_251.toBool() ? pyc_comb_169 : cache__tag__s2__w1);
    cache__tag__s2__w1__next = pyc_mux_253;
    pyc_mux_254 = (pyc_comb_251.toBool() ? pyc_comb_196 : cache__data__s2__w1);
    cache__data__s2__w1__next = pyc_mux_254;
    eval_comb_12();
    cache__rr__s3__next = pyc_comb_261;
    eval_comb_13();
    cache__valid__s3__w0__next = pyc_comb_265;
    pyc_mux_266 = (pyc_comb_264.toBool() ? pyc_comb_169 : cache__tag__s3__w0);
    cache__tag__s3__w0__next = pyc_mux_266;
    pyc_mux_267 = (pyc_comb_264.toBool() ? pyc_comb_196 : cache__data__s3__w0);
    cache__data__s3__w0__next = pyc_mux_267;
    eval_comb_14();
    cache__valid__s3__w1__next = pyc_comb_271;
    pyc_mux_272 = (pyc_comb_270.toBool() ? pyc_comb_169 : cache__tag__s3__w1);
    cache__tag__s3__w1__next = pyc_mux_272;
    pyc_mux_273 = (pyc_comb_270.toBool() ? pyc_comb_196 : cache__data__s3__w1);
    cache__data__s3__w1__next = pyc_mux_273;
    eval_comb_15();
    cache__rr__s4__next = pyc_comb_280;
    eval_comb_16();
    cache__valid__s4__w0__next = pyc_comb_284;
    pyc_mux_285 = (pyc_comb_283.toBool() ? pyc_comb_169 : cache__tag__s4__w0);
    cache__tag__s4__w0__next = pyc_mux_285;
    pyc_mux_286 = (pyc_comb_283.toBool() ? pyc_comb_196 : cache__data__s4__w0);
    cache__data__s4__w0__next = pyc_mux_286;
    eval_comb_17();
    cache__valid__s4__w1__next = pyc_comb_290;
    pyc_mux_291 = (pyc_comb_289.toBool() ? pyc_comb_169 : cache__tag__s4__w1);
    cache__tag__s4__w1__next = pyc_mux_291;
    pyc_mux_292 = (pyc_comb_289.toBool() ? pyc_comb_196 : cache__data__s4__w1);
    cache__data__s4__w1__next = pyc_mux_292;
    eval_comb_18();
    cache__rr__s5__next = pyc_comb_299;
    eval_comb_19();
    cache__valid__s5__w0__next = pyc_comb_303;
    pyc_mux_304 = (pyc_comb_302.toBool() ? pyc_comb_169 : cache__tag__s5__w0);
    cache__tag__s5__w0__next = pyc_mux_304;
    pyc_mux_305 = (pyc_comb_302.toBool() ? pyc_comb_196 : cache__data__s5__w0);
    cache__data__s5__w0__next = pyc_mux_305;
    eval_comb_20();
    cache__valid__s5__w1__next = pyc_comb_309;
    pyc_mux_310 = (pyc_comb_308.toBool() ? pyc_comb_169 : cache__tag__s5__w1);
    cache__tag__s5__w1__next = pyc_mux_310;
    pyc_mux_311 = (pyc_comb_308.toBool() ? pyc_comb_196 : cache__data__s5__w1);
    cache__data__s5__w1__next = pyc_mux_311;
    eval_comb_21();
    cache__rr__s6__next = pyc_comb_318;
    eval_comb_22();
    cache__valid__s6__w0__next = pyc_comb_322;
    pyc_mux_323 = (pyc_comb_321.toBool() ? pyc_comb_169 : cache__tag__s6__w0);
    cache__tag__s6__w0__next = pyc_mux_323;
    pyc_mux_324 = (pyc_comb_321.toBool() ? pyc_comb_196 : cache__data__s6__w0);
    cache__data__s6__w0__next = pyc_mux_324;
    eval_comb_23();
    cache__valid__s6__w1__next = pyc_comb_328;
    pyc_mux_329 = (pyc_comb_327.toBool() ? pyc_comb_169 : cache__tag__s6__w1);
    cache__tag__s6__w1__next = pyc_mux_329;
    pyc_mux_330 = (pyc_comb_327.toBool() ? pyc_comb_196 : cache__data__s6__w1);
    cache__data__s6__w1__next = pyc_mux_330;
    eval_comb_24();
    cache__rr__s7__next = pyc_comb_337;
    eval_comb_25();
    cache__valid__s7__w0__next = pyc_comb_341;
    pyc_mux_342 = (pyc_comb_340.toBool() ? pyc_comb_169 : cache__tag__s7__w0);
    cache__tag__s7__w0__next = pyc_mux_342;
    pyc_mux_343 = (pyc_comb_340.toBool() ? pyc_comb_196 : cache__data__s7__w0);
    cache__data__s7__w0__next = pyc_mux_343;
    eval_comb_26();
    cache__valid__s7__w1__next = pyc_comb_347;
    pyc_mux_348 = (pyc_comb_346.toBool() ? pyc_comb_169 : cache__tag__s7__w1);
    cache__tag__s7__w1__next = pyc_mux_348;
    pyc_mux_349 = (pyc_comb_346.toBool() ? pyc_comb_196 : cache__data__s7__w1);
    cache__data__s7__w1__next = pyc_mux_349;
    eval_comb_27();
    cache__req_q__in_valid = pyc_comb_26;
    cache__req_q__in_data = pyc_comb_27;
    cache__req_q__out_ready = pyc_fifo_36;
    cache__rsp_q__in_valid = pyc_comb_167;
    cache__rsp_q__in_data = pyc_comb_352;
    cache__rsp_q__out_ready = pyc_comb_28;
  }

  void eval() {
    eval_comb_pass();
    for (unsigned _i = 0; _i < 3u; ++_i) {
      pyc_fifo_33_inst.eval();
      pyc_fifo_36_inst.eval();
      main_mem.eval();
      eval_comb_pass();
    }
    req_ready = pyc_fifo_33;
    rsp_valid = pyc_fifo_37;
    rsp_hit = pyc_comb_41;
    rsp_rdata = pyc_comb_42;
  }

  void tick() {
    // Two-phase update: compute next state for all sequential elements,
    // then commit together. This avoids ordering artifacts between regs.
    // Phase 1: compute.
    pyc_fifo_33_inst.tick_compute();
    pyc_fifo_36_inst.tick_compute();
    pyc_reg_43_inst.tick_compute();
    pyc_reg_44_inst.tick_compute();
    pyc_reg_45_inst.tick_compute();
    pyc_reg_46_inst.tick_compute();
    pyc_reg_47_inst.tick_compute();
    pyc_reg_48_inst.tick_compute();
    pyc_reg_49_inst.tick_compute();
    pyc_reg_50_inst.tick_compute();
    pyc_reg_51_inst.tick_compute();
    pyc_reg_52_inst.tick_compute();
    pyc_reg_53_inst.tick_compute();
    pyc_reg_54_inst.tick_compute();
    pyc_reg_55_inst.tick_compute();
    pyc_reg_56_inst.tick_compute();
    pyc_reg_57_inst.tick_compute();
    pyc_reg_58_inst.tick_compute();
    pyc_reg_59_inst.tick_compute();
    pyc_reg_60_inst.tick_compute();
    pyc_reg_61_inst.tick_compute();
    pyc_reg_62_inst.tick_compute();
    pyc_reg_63_inst.tick_compute();
    pyc_reg_64_inst.tick_compute();
    pyc_reg_65_inst.tick_compute();
    pyc_reg_66_inst.tick_compute();
    pyc_reg_67_inst.tick_compute();
    pyc_reg_68_inst.tick_compute();
    pyc_reg_69_inst.tick_compute();
    pyc_reg_70_inst.tick_compute();
    pyc_reg_71_inst.tick_compute();
    pyc_reg_72_inst.tick_compute();
    pyc_reg_73_inst.tick_compute();
    pyc_reg_74_inst.tick_compute();
    pyc_reg_75_inst.tick_compute();
    pyc_reg_76_inst.tick_compute();
    pyc_reg_77_inst.tick_compute();
    pyc_reg_78_inst.tick_compute();
    pyc_reg_79_inst.tick_compute();
    pyc_reg_80_inst.tick_compute();
    pyc_reg_81_inst.tick_compute();
    pyc_reg_82_inst.tick_compute();
    pyc_reg_83_inst.tick_compute();
    pyc_reg_84_inst.tick_compute();
    pyc_reg_85_inst.tick_compute();
    pyc_reg_86_inst.tick_compute();
    pyc_reg_87_inst.tick_compute();
    pyc_reg_88_inst.tick_compute();
    pyc_reg_89_inst.tick_compute();
    pyc_reg_90_inst.tick_compute();
    pyc_reg_91_inst.tick_compute();
    pyc_reg_92_inst.tick_compute();
    pyc_reg_93_inst.tick_compute();
    pyc_reg_94_inst.tick_compute();
    pyc_reg_95_inst.tick_compute();
    pyc_reg_96_inst.tick_compute();
    pyc_reg_97_inst.tick_compute();
    pyc_reg_98_inst.tick_compute();
    main_mem.tick_compute();
    // Phase 2: commit.
    pyc_fifo_33_inst.tick_commit();
    pyc_fifo_36_inst.tick_commit();
    pyc_reg_43_inst.tick_commit();
    pyc_reg_44_inst.tick_commit();
    pyc_reg_45_inst.tick_commit();
    pyc_reg_46_inst.tick_commit();
    pyc_reg_47_inst.tick_commit();
    pyc_reg_48_inst.tick_commit();
    pyc_reg_49_inst.tick_commit();
    pyc_reg_50_inst.tick_commit();
    pyc_reg_51_inst.tick_commit();
    pyc_reg_52_inst.tick_commit();
    pyc_reg_53_inst.tick_commit();
    pyc_reg_54_inst.tick_commit();
    pyc_reg_55_inst.tick_commit();
    pyc_reg_56_inst.tick_commit();
    pyc_reg_57_inst.tick_commit();
    pyc_reg_58_inst.tick_commit();
    pyc_reg_59_inst.tick_commit();
    pyc_reg_60_inst.tick_commit();
    pyc_reg_61_inst.tick_commit();
    pyc_reg_62_inst.tick_commit();
    pyc_reg_63_inst.tick_commit();
    pyc_reg_64_inst.tick_commit();
    pyc_reg_65_inst.tick_commit();
    pyc_reg_66_inst.tick_commit();
    pyc_reg_67_inst.tick_commit();
    pyc_reg_68_inst.tick_commit();
    pyc_reg_69_inst.tick_commit();
    pyc_reg_70_inst.tick_commit();
    pyc_reg_71_inst.tick_commit();
    pyc_reg_72_inst.tick_commit();
    pyc_reg_73_inst.tick_commit();
    pyc_reg_74_inst.tick_commit();
    pyc_reg_75_inst.tick_commit();
    pyc_reg_76_inst.tick_commit();
    pyc_reg_77_inst.tick_commit();
    pyc_reg_78_inst.tick_commit();
    pyc_reg_79_inst.tick_commit();
    pyc_reg_80_inst.tick_commit();
    pyc_reg_81_inst.tick_commit();
    pyc_reg_82_inst.tick_commit();
    pyc_reg_83_inst.tick_commit();
    pyc_reg_84_inst.tick_commit();
    pyc_reg_85_inst.tick_commit();
    pyc_reg_86_inst.tick_commit();
    pyc_reg_87_inst.tick_commit();
    pyc_reg_88_inst.tick_commit();
    pyc_reg_89_inst.tick_commit();
    pyc_reg_90_inst.tick_commit();
    pyc_reg_91_inst.tick_commit();
    pyc_reg_92_inst.tick_commit();
    pyc_reg_93_inst.tick_commit();
    pyc_reg_94_inst.tick_commit();
    pyc_reg_95_inst.tick_commit();
    pyc_reg_96_inst.tick_commit();
    pyc_reg_97_inst.tick_commit();
    pyc_reg_98_inst.tick_commit();
    main_mem.tick_commit();
  }
};

} // namespace pyc::gen
