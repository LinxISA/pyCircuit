// pyCircuit C++ emission (prototype)
#include <pyc/cpp/pyc_sim.hpp>

namespace pyc::gen {

struct digital_clock {
  pyc::cpp::Wire<1> clk{};
  pyc::cpp::Wire<1> rst{};
  pyc::cpp::Wire<1> btn_set{};
  pyc::cpp::Wire<1> btn_plus{};
  pyc::cpp::Wire<1> btn_minus{};
  pyc::cpp::Wire<8> hours_bcd{};
  pyc::cpp::Wire<8> minutes_bcd{};
  pyc::cpp::Wire<8> seconds_bcd{};
  pyc::cpp::Wire<2> setting_mode{};
  pyc::cpp::Wire<1> colon_blink{};

  pyc::cpp::Wire<1> blink{};
  pyc::cpp::Wire<5> db_minus_cnt{};
  pyc::cpp::Wire<1> db_minus_prev{};
  pyc::cpp::Wire<1> db_minus_stable{};
  pyc::cpp::Wire<1> db_minus_stable_prev{};
  pyc::cpp::Wire<5> db_plus_cnt{};
  pyc::cpp::Wire<1> db_plus_prev{};
  pyc::cpp::Wire<1> db_plus_stable{};
  pyc::cpp::Wire<1> db_plus_stable_prev{};
  pyc::cpp::Wire<5> db_set_cnt{};
  pyc::cpp::Wire<1> db_set_prev{};
  pyc::cpp::Wire<1> db_set_stable{};
  pyc::cpp::Wire<1> db_set_stable_prev{};
  pyc::cpp::Wire<5> hours{};
  pyc::cpp::Wire<4> hr_tens{};
  pyc::cpp::Wire<4> min_tens{};
  pyc::cpp::Wire<6> minutes{};
  pyc::cpp::Wire<2> mode{};
  pyc::cpp::Wire<10> prescaler{};
  pyc::cpp::Wire<6> pyc_add_103{};
  pyc::cpp::Wire<6> pyc_add_111{};
  pyc::cpp::Wire<5> pyc_add_118{};
  pyc::cpp::Wire<6> pyc_add_153{};
  pyc::cpp::Wire<6> pyc_add_176{};
  pyc::cpp::Wire<5> pyc_add_193{};
  pyc::cpp::Wire<5> pyc_add_62{};
  pyc::cpp::Wire<5> pyc_add_74{};
  pyc::cpp::Wire<5> pyc_add_86{};
  pyc::cpp::Wire<10> pyc_add_93{};
  pyc::cpp::Wire<2> pyc_add_99{};
  pyc::cpp::Wire<1> pyc_and_107{};
  pyc::cpp::Wire<1> pyc_and_108{};
  pyc::cpp::Wire<1> pyc_and_115{};
  pyc::cpp::Wire<1> pyc_and_204{};
  pyc::cpp::Wire<1> pyc_and_207{};
  pyc::cpp::Wire<1> pyc_and_210{};
  pyc::cpp::Wire<1> pyc_and_213{};
  pyc::cpp::Wire<1> pyc_and_216{};
  pyc::cpp::Wire<1> pyc_and_219{};
  pyc::cpp::Wire<1> pyc_and_222{};
  pyc::cpp::Wire<1> pyc_and_67{};
  pyc::cpp::Wire<1> pyc_and_79{};
  pyc::cpp::Wire<1> pyc_and_91{};
  pyc::cpp::Wire<5> pyc_comb_122{};
  pyc::cpp::Wire<1> pyc_comb_123{};
  pyc::cpp::Wire<1> pyc_comb_124{};
  pyc::cpp::Wire<1> pyc_comb_125{};
  pyc::cpp::Wire<10> pyc_comb_126{};
  pyc::cpp::Wire<1> pyc_comb_127{};
  pyc::cpp::Wire<1> pyc_comb_128{};
  pyc::cpp::Wire<1> pyc_comb_129{};
  pyc::cpp::Wire<1> pyc_comb_130{};
  pyc::cpp::Wire<2> pyc_comb_131{};
  pyc::cpp::Wire<6> pyc_comb_132{};
  pyc::cpp::Wire<6> pyc_comb_133{};
  pyc::cpp::Wire<1> pyc_comb_134{};
  pyc::cpp::Wire<6> pyc_comb_135{};
  pyc::cpp::Wire<6> pyc_comb_136{};
  pyc::cpp::Wire<1> pyc_comb_137{};
  pyc::cpp::Wire<5> pyc_comb_138{};
  pyc::cpp::Wire<5> pyc_comb_139{};
  pyc::cpp::Wire<1> pyc_comb_157{};
  pyc::cpp::Wire<1> pyc_comb_158{};
  pyc::cpp::Wire<1> pyc_comb_159{};
  pyc::cpp::Wire<1> pyc_comb_160{};
  pyc::cpp::Wire<1> pyc_comb_161{};
  pyc::cpp::Wire<8> pyc_comb_162{};
  pyc::cpp::Wire<1> pyc_comb_180{};
  pyc::cpp::Wire<1> pyc_comb_181{};
  pyc::cpp::Wire<1> pyc_comb_182{};
  pyc::cpp::Wire<1> pyc_comb_183{};
  pyc::cpp::Wire<1> pyc_comb_184{};
  pyc::cpp::Wire<8> pyc_comb_185{};
  pyc::cpp::Wire<1> pyc_comb_198{};
  pyc::cpp::Wire<1> pyc_comb_199{};
  pyc::cpp::Wire<8> pyc_comb_200{};
  pyc::cpp::Wire<1> pyc_comb_201{};
  pyc::cpp::Wire<6> pyc_comb_227{};
  pyc::cpp::Wire<6> pyc_comb_232{};
  pyc::cpp::Wire<5> pyc_comb_237{};
  pyc::cpp::Wire<4> pyc_comb_260{};
  pyc::cpp::Wire<4> pyc_comb_266{};
  pyc::cpp::Wire<4> pyc_comb_269{};
  pyc::cpp::Wire<1> pyc_comb_30{};
  pyc::cpp::Wire<1> pyc_comb_31{};
  pyc::cpp::Wire<5> pyc_comb_32{};
  pyc::cpp::Wire<5> pyc_comb_33{};
  pyc::cpp::Wire<6> pyc_comb_34{};
  pyc::cpp::Wire<4> pyc_comb_35{};
  pyc::cpp::Wire<6> pyc_comb_36{};
  pyc::cpp::Wire<4> pyc_comb_37{};
  pyc::cpp::Wire<6> pyc_comb_38{};
  pyc::cpp::Wire<4> pyc_comb_39{};
  pyc::cpp::Wire<6> pyc_comb_40{};
  pyc::cpp::Wire<4> pyc_comb_41{};
  pyc::cpp::Wire<6> pyc_comb_42{};
  pyc::cpp::Wire<4> pyc_comb_43{};
  pyc::cpp::Wire<4> pyc_comb_44{};
  pyc::cpp::Wire<5> pyc_comb_45{};
  pyc::cpp::Wire<6> pyc_comb_46{};
  pyc::cpp::Wire<6> pyc_comb_47{};
  pyc::cpp::Wire<6> pyc_comb_48{};
  pyc::cpp::Wire<2> pyc_comb_49{};
  pyc::cpp::Wire<2> pyc_comb_50{};
  pyc::cpp::Wire<2> pyc_comb_51{};
  pyc::cpp::Wire<2> pyc_comb_52{};
  pyc::cpp::Wire<10> pyc_comb_53{};
  pyc::cpp::Wire<10> pyc_comb_54{};
  pyc::cpp::Wire<10> pyc_comb_55{};
  pyc::cpp::Wire<5> pyc_comb_56{};
  pyc::cpp::Wire<5> pyc_comb_57{};
  pyc::cpp::Wire<5> pyc_comb_58{};
  pyc::cpp::Wire<5> pyc_comb_68{};
  pyc::cpp::Wire<1> pyc_comb_69{};
  pyc::cpp::Wire<1> pyc_comb_70{};
  pyc::cpp::Wire<5> pyc_comb_80{};
  pyc::cpp::Wire<1> pyc_comb_81{};
  pyc::cpp::Wire<1> pyc_comb_82{};
  pyc::cpp::Wire<8> pyc_concat_156{};
  pyc::cpp::Wire<8> pyc_concat_179{};
  pyc::cpp::Wire<8> pyc_concat_196{};
  pyc::cpp::Wire<1> pyc_constant_1{};
  pyc::cpp::Wire<4> pyc_constant_10{};
  pyc::cpp::Wire<6> pyc_constant_11{};
  pyc::cpp::Wire<4> pyc_constant_12{};
  pyc::cpp::Wire<6> pyc_constant_13{};
  pyc::cpp::Wire<4> pyc_constant_14{};
  pyc::cpp::Wire<4> pyc_constant_15{};
  pyc::cpp::Wire<5> pyc_constant_16{};
  pyc::cpp::Wire<6> pyc_constant_17{};
  pyc::cpp::Wire<6> pyc_constant_18{};
  pyc::cpp::Wire<6> pyc_constant_19{};
  pyc::cpp::Wire<1> pyc_constant_2{};
  pyc::cpp::Wire<2> pyc_constant_20{};
  pyc::cpp::Wire<2> pyc_constant_21{};
  pyc::cpp::Wire<2> pyc_constant_22{};
  pyc::cpp::Wire<2> pyc_constant_23{};
  pyc::cpp::Wire<10> pyc_constant_24{};
  pyc::cpp::Wire<10> pyc_constant_25{};
  pyc::cpp::Wire<10> pyc_constant_26{};
  pyc::cpp::Wire<5> pyc_constant_27{};
  pyc::cpp::Wire<5> pyc_constant_28{};
  pyc::cpp::Wire<5> pyc_constant_29{};
  pyc::cpp::Wire<5> pyc_constant_3{};
  pyc::cpp::Wire<5> pyc_constant_4{};
  pyc::cpp::Wire<6> pyc_constant_5{};
  pyc::cpp::Wire<4> pyc_constant_6{};
  pyc::cpp::Wire<6> pyc_constant_7{};
  pyc::cpp::Wire<4> pyc_constant_8{};
  pyc::cpp::Wire<6> pyc_constant_9{};
  pyc::cpp::Wire<1> pyc_eq_101{};
  pyc::cpp::Wire<1> pyc_eq_102{};
  pyc::cpp::Wire<1> pyc_eq_109{};
  pyc::cpp::Wire<1> pyc_eq_110{};
  pyc::cpp::Wire<1> pyc_eq_116{};
  pyc::cpp::Wire<1> pyc_eq_117{};
  pyc::cpp::Wire<1> pyc_eq_59{};
  pyc::cpp::Wire<1> pyc_eq_61{};
  pyc::cpp::Wire<1> pyc_eq_71{};
  pyc::cpp::Wire<1> pyc_eq_73{};
  pyc::cpp::Wire<1> pyc_eq_83{};
  pyc::cpp::Wire<1> pyc_eq_85{};
  pyc::cpp::Wire<1> pyc_eq_92{};
  pyc::cpp::Wire<1> pyc_eq_95{};
  pyc::cpp::Wire<1> pyc_eq_96{};
  pyc::cpp::Wire<1> pyc_eq_97{};
  pyc::cpp::Wire<1> pyc_eq_98{};
  pyc::cpp::Wire<2> pyc_mux_100{};
  pyc::cpp::Wire<6> pyc_mux_104{};
  pyc::cpp::Wire<6> pyc_mux_106{};
  pyc::cpp::Wire<6> pyc_mux_112{};
  pyc::cpp::Wire<6> pyc_mux_114{};
  pyc::cpp::Wire<5> pyc_mux_119{};
  pyc::cpp::Wire<5> pyc_mux_121{};
  pyc::cpp::Wire<6> pyc_mux_224{};
  pyc::cpp::Wire<6> pyc_mux_225{};
  pyc::cpp::Wire<6> pyc_mux_226{};
  pyc::cpp::Wire<6> pyc_mux_229{};
  pyc::cpp::Wire<6> pyc_mux_230{};
  pyc::cpp::Wire<6> pyc_mux_231{};
  pyc::cpp::Wire<5> pyc_mux_234{};
  pyc::cpp::Wire<5> pyc_mux_235{};
  pyc::cpp::Wire<5> pyc_mux_236{};
  pyc::cpp::Wire<2> pyc_mux_239{};
  pyc::cpp::Wire<1> pyc_mux_241{};
  pyc::cpp::Wire<4> pyc_mux_255{};
  pyc::cpp::Wire<4> pyc_mux_256{};
  pyc::cpp::Wire<4> pyc_mux_257{};
  pyc::cpp::Wire<4> pyc_mux_258{};
  pyc::cpp::Wire<4> pyc_mux_259{};
  pyc::cpp::Wire<4> pyc_mux_261{};
  pyc::cpp::Wire<4> pyc_mux_262{};
  pyc::cpp::Wire<4> pyc_mux_263{};
  pyc::cpp::Wire<4> pyc_mux_264{};
  pyc::cpp::Wire<4> pyc_mux_265{};
  pyc::cpp::Wire<4> pyc_mux_267{};
  pyc::cpp::Wire<4> pyc_mux_268{};
  pyc::cpp::Wire<5> pyc_mux_63{};
  pyc::cpp::Wire<5> pyc_mux_64{};
  pyc::cpp::Wire<1> pyc_mux_65{};
  pyc::cpp::Wire<5> pyc_mux_75{};
  pyc::cpp::Wire<5> pyc_mux_76{};
  pyc::cpp::Wire<1> pyc_mux_77{};
  pyc::cpp::Wire<5> pyc_mux_87{};
  pyc::cpp::Wire<5> pyc_mux_88{};
  pyc::cpp::Wire<1> pyc_mux_89{};
  pyc::cpp::Wire<10> pyc_mux_94{};
  pyc::cpp::Wire<1> pyc_not_141{};
  pyc::cpp::Wire<1> pyc_not_143{};
  pyc::cpp::Wire<1> pyc_not_145{};
  pyc::cpp::Wire<1> pyc_not_147{};
  pyc::cpp::Wire<1> pyc_not_149{};
  pyc::cpp::Wire<1> pyc_not_164{};
  pyc::cpp::Wire<1> pyc_not_166{};
  pyc::cpp::Wire<1> pyc_not_168{};
  pyc::cpp::Wire<1> pyc_not_170{};
  pyc::cpp::Wire<1> pyc_not_172{};
  pyc::cpp::Wire<1> pyc_not_187{};
  pyc::cpp::Wire<1> pyc_not_189{};
  pyc::cpp::Wire<1> pyc_not_197{};
  pyc::cpp::Wire<1> pyc_not_60{};
  pyc::cpp::Wire<1> pyc_not_66{};
  pyc::cpp::Wire<1> pyc_not_72{};
  pyc::cpp::Wire<1> pyc_not_78{};
  pyc::cpp::Wire<1> pyc_not_84{};
  pyc::cpp::Wire<1> pyc_not_90{};
  pyc::cpp::Wire<1> pyc_reg_202{};
  pyc::cpp::Wire<1> pyc_reg_203{};
  pyc::cpp::Wire<1> pyc_reg_205{};
  pyc::cpp::Wire<1> pyc_reg_206{};
  pyc::cpp::Wire<1> pyc_reg_208{};
  pyc::cpp::Wire<1> pyc_reg_209{};
  pyc::cpp::Wire<1> pyc_reg_211{};
  pyc::cpp::Wire<1> pyc_reg_212{};
  pyc::cpp::Wire<1> pyc_reg_214{};
  pyc::cpp::Wire<1> pyc_reg_215{};
  pyc::cpp::Wire<1> pyc_reg_217{};
  pyc::cpp::Wire<1> pyc_reg_218{};
  pyc::cpp::Wire<1> pyc_reg_220{};
  pyc::cpp::Wire<1> pyc_reg_221{};
  pyc::cpp::Wire<10> pyc_reg_223{};
  pyc::cpp::Wire<6> pyc_reg_228{};
  pyc::cpp::Wire<6> pyc_reg_233{};
  pyc::cpp::Wire<5> pyc_reg_238{};
  pyc::cpp::Wire<2> pyc_reg_240{};
  pyc::cpp::Wire<1> pyc_reg_242{};
  pyc::cpp::Wire<5> pyc_reg_243{};
  pyc::cpp::Wire<1> pyc_reg_244{};
  pyc::cpp::Wire<1> pyc_reg_245{};
  pyc::cpp::Wire<1> pyc_reg_246{};
  pyc::cpp::Wire<5> pyc_reg_247{};
  pyc::cpp::Wire<1> pyc_reg_248{};
  pyc::cpp::Wire<1> pyc_reg_249{};
  pyc::cpp::Wire<1> pyc_reg_250{};
  pyc::cpp::Wire<5> pyc_reg_251{};
  pyc::cpp::Wire<1> pyc_reg_252{};
  pyc::cpp::Wire<1> pyc_reg_253{};
  pyc::cpp::Wire<1> pyc_reg_254{};
  pyc::cpp::Wire<6> pyc_shli_151{};
  pyc::cpp::Wire<6> pyc_shli_152{};
  pyc::cpp::Wire<6> pyc_shli_174{};
  pyc::cpp::Wire<6> pyc_shli_175{};
  pyc::cpp::Wire<5> pyc_shli_191{};
  pyc::cpp::Wire<5> pyc_shli_192{};
  pyc::cpp::Wire<6> pyc_sub_105{};
  pyc::cpp::Wire<6> pyc_sub_113{};
  pyc::cpp::Wire<5> pyc_sub_120{};
  pyc::cpp::Wire<6> pyc_sub_154{};
  pyc::cpp::Wire<6> pyc_sub_177{};
  pyc::cpp::Wire<5> pyc_sub_194{};
  pyc::cpp::Wire<4> pyc_trunc_155{};
  pyc::cpp::Wire<4> pyc_trunc_178{};
  pyc::cpp::Wire<4> pyc_trunc_195{};
  pyc::cpp::Wire<1> pyc_ult_140{};
  pyc::cpp::Wire<1> pyc_ult_142{};
  pyc::cpp::Wire<1> pyc_ult_144{};
  pyc::cpp::Wire<1> pyc_ult_146{};
  pyc::cpp::Wire<1> pyc_ult_148{};
  pyc::cpp::Wire<1> pyc_ult_163{};
  pyc::cpp::Wire<1> pyc_ult_165{};
  pyc::cpp::Wire<1> pyc_ult_167{};
  pyc::cpp::Wire<1> pyc_ult_169{};
  pyc::cpp::Wire<1> pyc_ult_171{};
  pyc::cpp::Wire<1> pyc_ult_186{};
  pyc::cpp::Wire<1> pyc_ult_188{};
  pyc::cpp::Wire<6> pyc_zext_150{};
  pyc::cpp::Wire<6> pyc_zext_173{};
  pyc::cpp::Wire<5> pyc_zext_190{};
  pyc::cpp::Wire<4> sec_tens{};
  pyc::cpp::Wire<6> seconds{};

  pyc::cpp::pyc_reg<1> pyc_reg_202_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_203_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_205_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_206_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_208_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_209_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_211_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_212_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_214_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_215_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_217_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_218_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_220_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_221_inst;
  pyc::cpp::pyc_reg<10> pyc_reg_223_inst;
  pyc::cpp::pyc_reg<6> pyc_reg_228_inst;
  pyc::cpp::pyc_reg<6> pyc_reg_233_inst;
  pyc::cpp::pyc_reg<5> pyc_reg_238_inst;
  pyc::cpp::pyc_reg<2> pyc_reg_240_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_242_inst;
  pyc::cpp::pyc_reg<5> pyc_reg_243_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_244_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_245_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_246_inst;
  pyc::cpp::pyc_reg<5> pyc_reg_247_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_248_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_249_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_250_inst;
  pyc::cpp::pyc_reg<5> pyc_reg_251_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_252_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_253_inst;
  pyc::cpp::pyc_reg<1> pyc_reg_254_inst;

  digital_clock() :
      pyc_reg_202_inst(clk, rst, pyc_comb_31, pyc_comb_125, pyc_comb_30, pyc_reg_202),
      pyc_reg_203_inst(clk, rst, pyc_comb_31, pyc_comb_127, pyc_comb_30, pyc_reg_203),
      pyc_reg_205_inst(clk, rst, pyc_comb_31, pyc_comb_82, pyc_comb_30, pyc_reg_205),
      pyc_reg_206_inst(clk, rst, pyc_comb_31, pyc_comb_130, pyc_comb_30, pyc_reg_206),
      pyc_reg_208_inst(clk, rst, pyc_comb_31, pyc_comb_124, pyc_comb_30, pyc_reg_208),
      pyc_reg_209_inst(clk, rst, pyc_comb_31, pyc_comb_130, pyc_comb_30, pyc_reg_209),
      pyc_reg_211_inst(clk, rst, pyc_comb_31, pyc_comb_82, pyc_comb_30, pyc_reg_211),
      pyc_reg_212_inst(clk, rst, pyc_comb_31, pyc_comb_129, pyc_comb_30, pyc_reg_212),
      pyc_reg_214_inst(clk, rst, pyc_comb_31, pyc_comb_124, pyc_comb_30, pyc_reg_214),
      pyc_reg_215_inst(clk, rst, pyc_comb_31, pyc_comb_129, pyc_comb_30, pyc_reg_215),
      pyc_reg_217_inst(clk, rst, pyc_comb_31, pyc_comb_82, pyc_comb_30, pyc_reg_217),
      pyc_reg_218_inst(clk, rst, pyc_comb_31, pyc_comb_128, pyc_comb_30, pyc_reg_218),
      pyc_reg_220_inst(clk, rst, pyc_comb_31, pyc_comb_124, pyc_comb_30, pyc_reg_220),
      pyc_reg_221_inst(clk, rst, pyc_comb_31, pyc_comb_128, pyc_comb_30, pyc_reg_221),
      pyc_reg_223_inst(clk, rst, pyc_comb_31, pyc_comb_126, pyc_comb_54, pyc_reg_223),
      pyc_reg_228_inst(clk, rst, pyc_comb_31, pyc_comb_227, pyc_comb_47, pyc_reg_228),
      pyc_reg_233_inst(clk, rst, pyc_comb_31, pyc_comb_232, pyc_comb_47, pyc_reg_233),
      pyc_reg_238_inst(clk, rst, pyc_comb_31, pyc_comb_237, pyc_comb_57, pyc_reg_238),
      pyc_reg_240_inst(clk, rst, pyc_comb_31, pyc_mux_239, pyc_comb_52, pyc_reg_240),
      pyc_reg_242_inst(clk, rst, pyc_comb_31, pyc_mux_241, pyc_comb_30, pyc_reg_242),
      pyc_reg_243_inst(clk, rst, pyc_comb_31, pyc_comb_68, pyc_comb_57, pyc_reg_243),
      pyc_reg_244_inst(clk, rst, pyc_comb_31, btn_set, pyc_comb_30, pyc_reg_244),
      pyc_reg_245_inst(clk, rst, pyc_comb_31, pyc_comb_69, pyc_comb_30, pyc_reg_245),
      pyc_reg_246_inst(clk, rst, pyc_comb_31, db_set_stable, pyc_comb_30, pyc_reg_246),
      pyc_reg_247_inst(clk, rst, pyc_comb_31, pyc_comb_80, pyc_comb_57, pyc_reg_247),
      pyc_reg_248_inst(clk, rst, pyc_comb_31, btn_plus, pyc_comb_30, pyc_reg_248),
      pyc_reg_249_inst(clk, rst, pyc_comb_31, pyc_comb_81, pyc_comb_30, pyc_reg_249),
      pyc_reg_250_inst(clk, rst, pyc_comb_31, db_plus_stable, pyc_comb_30, pyc_reg_250),
      pyc_reg_251_inst(clk, rst, pyc_comb_31, pyc_comb_122, pyc_comb_57, pyc_reg_251),
      pyc_reg_252_inst(clk, rst, pyc_comb_31, btn_minus, pyc_comb_30, pyc_reg_252),
      pyc_reg_253_inst(clk, rst, pyc_comb_31, pyc_comb_123, pyc_comb_30, pyc_reg_253),
      pyc_reg_254_inst(clk, rst, pyc_comb_31, db_minus_stable, pyc_comb_30, pyc_reg_254) {
    eval();
  }

  inline void eval_comb_0() {
    pyc_eq_83 = pyc::cpp::Wire<1>((btn_minus == db_minus_prev) ? 1u : 0u);
    pyc_not_84 = (~pyc_eq_83);
    pyc_eq_85 = pyc::cpp::Wire<1>((db_minus_cnt == pyc_comb_58) ? 1u : 0u);
    pyc_add_86 = (db_minus_cnt + pyc_comb_56);
    pyc_mux_87 = (pyc_eq_85.toBool() ? db_minus_cnt : pyc_add_86);
    pyc_mux_88 = (pyc_not_84.toBool() ? pyc_comb_57 : pyc_mux_87);
    pyc_mux_89 = (pyc_eq_85.toBool() ? btn_minus : db_minus_stable);
    pyc_not_90 = (~db_minus_stable_prev);
    pyc_and_91 = (db_minus_stable & pyc_not_90);
    pyc_eq_92 = pyc::cpp::Wire<1>((prescaler == pyc_comb_55) ? 1u : 0u);
    pyc_add_93 = (prescaler + pyc_comb_53);
    pyc_mux_94 = (pyc_eq_92.toBool() ? pyc_comb_54 : pyc_add_93);
    pyc_eq_95 = pyc::cpp::Wire<1>((mode == pyc_comb_52) ? 1u : 0u);
    pyc_eq_96 = pyc::cpp::Wire<1>((mode == pyc_comb_51) ? 1u : 0u);
    pyc_eq_97 = pyc::cpp::Wire<1>((mode == pyc_comb_50) ? 1u : 0u);
    pyc_eq_98 = pyc::cpp::Wire<1>((mode == pyc_comb_49) ? 1u : 0u);
    pyc_add_99 = (mode + pyc_comb_51);
    pyc_mux_100 = (pyc_eq_98.toBool() ? pyc_comb_52 : pyc_add_99);
    pyc_eq_101 = pyc::cpp::Wire<1>((seconds == pyc_comb_48) ? 1u : 0u);
    pyc_eq_102 = pyc::cpp::Wire<1>((seconds == pyc_comb_47) ? 1u : 0u);
    pyc_add_103 = (seconds + pyc_comb_46);
    pyc_mux_104 = (pyc_eq_101.toBool() ? pyc_comb_47 : pyc_add_103);
    pyc_sub_105 = (seconds - pyc_comb_46);
    pyc_mux_106 = (pyc_eq_102.toBool() ? pyc_comb_48 : pyc_sub_105);
    pyc_and_107 = (pyc_eq_92 & pyc_eq_101);
    pyc_and_108 = (pyc_and_107 & pyc_eq_95);
    pyc_eq_109 = pyc::cpp::Wire<1>((minutes == pyc_comb_48) ? 1u : 0u);
    pyc_eq_110 = pyc::cpp::Wire<1>((minutes == pyc_comb_47) ? 1u : 0u);
    pyc_add_111 = (minutes + pyc_comb_46);
    pyc_mux_112 = (pyc_eq_109.toBool() ? pyc_comb_47 : pyc_add_111);
    pyc_sub_113 = (minutes - pyc_comb_46);
    pyc_mux_114 = (pyc_eq_110.toBool() ? pyc_comb_48 : pyc_sub_113);
    pyc_and_115 = (pyc_and_108 & pyc_eq_109);
    pyc_eq_116 = pyc::cpp::Wire<1>((hours == pyc_comb_45) ? 1u : 0u);
    pyc_eq_117 = pyc::cpp::Wire<1>((hours == pyc_comb_57) ? 1u : 0u);
    pyc_add_118 = (hours + pyc_comb_56);
    pyc_mux_119 = (pyc_eq_116.toBool() ? pyc_comb_57 : pyc_add_118);
    pyc_sub_120 = (hours - pyc_comb_56);
    pyc_mux_121 = (pyc_eq_117.toBool() ? pyc_comb_45 : pyc_sub_120);
    pyc_comb_122 = pyc_mux_88;
    pyc_comb_123 = pyc_mux_89;
    pyc_comb_124 = pyc_and_91;
    pyc_comb_125 = pyc_eq_92;
    pyc_comb_126 = pyc_mux_94;
    pyc_comb_127 = pyc_eq_95;
    pyc_comb_128 = pyc_eq_96;
    pyc_comb_129 = pyc_eq_97;
    pyc_comb_130 = pyc_eq_98;
    pyc_comb_131 = pyc_mux_100;
    pyc_comb_132 = pyc_mux_104;
    pyc_comb_133 = pyc_mux_106;
    pyc_comb_134 = pyc_and_108;
    pyc_comb_135 = pyc_mux_112;
    pyc_comb_136 = pyc_mux_114;
    pyc_comb_137 = pyc_and_115;
    pyc_comb_138 = pyc_mux_119;
    pyc_comb_139 = pyc_mux_121;
  }

  inline void eval_comb_1() {
    pyc_ult_140 = pyc::cpp::Wire<1>((seconds < pyc_comb_42) ? 1u : 0u);
    pyc_not_141 = (~pyc_ult_140);
    pyc_ult_142 = pyc::cpp::Wire<1>((seconds < pyc_comb_40) ? 1u : 0u);
    pyc_not_143 = (~pyc_ult_142);
    pyc_ult_144 = pyc::cpp::Wire<1>((seconds < pyc_comb_38) ? 1u : 0u);
    pyc_not_145 = (~pyc_ult_144);
    pyc_ult_146 = pyc::cpp::Wire<1>((seconds < pyc_comb_36) ? 1u : 0u);
    pyc_not_147 = (~pyc_ult_146);
    pyc_ult_148 = pyc::cpp::Wire<1>((seconds < pyc_comb_34) ? 1u : 0u);
    pyc_not_149 = (~pyc_ult_148);
    pyc_zext_150 = pyc::cpp::zext<6, 4>(sec_tens);
    pyc_shli_151 = pyc::cpp::shl<6>(pyc_zext_150, 3u);
    pyc_shli_152 = pyc::cpp::shl<6>(pyc_zext_150, 1u);
    pyc_add_153 = (pyc_shli_151 + pyc_shli_152);
    pyc_sub_154 = (seconds - pyc_add_153);
    pyc_trunc_155 = pyc::cpp::trunc<4, 6>(pyc_sub_154);
    pyc_concat_156 = pyc::cpp::concat(sec_tens, pyc_trunc_155);
    pyc_comb_157 = pyc_not_141;
    pyc_comb_158 = pyc_not_143;
    pyc_comb_159 = pyc_not_145;
    pyc_comb_160 = pyc_not_147;
    pyc_comb_161 = pyc_not_149;
    pyc_comb_162 = pyc_concat_156;
  }

  inline void eval_comb_2() {
    pyc_ult_163 = pyc::cpp::Wire<1>((minutes < pyc_comb_42) ? 1u : 0u);
    pyc_not_164 = (~pyc_ult_163);
    pyc_ult_165 = pyc::cpp::Wire<1>((minutes < pyc_comb_40) ? 1u : 0u);
    pyc_not_166 = (~pyc_ult_165);
    pyc_ult_167 = pyc::cpp::Wire<1>((minutes < pyc_comb_38) ? 1u : 0u);
    pyc_not_168 = (~pyc_ult_167);
    pyc_ult_169 = pyc::cpp::Wire<1>((minutes < pyc_comb_36) ? 1u : 0u);
    pyc_not_170 = (~pyc_ult_169);
    pyc_ult_171 = pyc::cpp::Wire<1>((minutes < pyc_comb_34) ? 1u : 0u);
    pyc_not_172 = (~pyc_ult_171);
    pyc_zext_173 = pyc::cpp::zext<6, 4>(min_tens);
    pyc_shli_174 = pyc::cpp::shl<6>(pyc_zext_173, 3u);
    pyc_shli_175 = pyc::cpp::shl<6>(pyc_zext_173, 1u);
    pyc_add_176 = (pyc_shli_174 + pyc_shli_175);
    pyc_sub_177 = (minutes - pyc_add_176);
    pyc_trunc_178 = pyc::cpp::trunc<4, 6>(pyc_sub_177);
    pyc_concat_179 = pyc::cpp::concat(min_tens, pyc_trunc_178);
    pyc_comb_180 = pyc_not_164;
    pyc_comb_181 = pyc_not_166;
    pyc_comb_182 = pyc_not_168;
    pyc_comb_183 = pyc_not_170;
    pyc_comb_184 = pyc_not_172;
    pyc_comb_185 = pyc_concat_179;
  }

  inline void eval_comb_3() {
    pyc_ult_186 = pyc::cpp::Wire<1>((hours < pyc_comb_33) ? 1u : 0u);
    pyc_not_187 = (~pyc_ult_186);
    pyc_ult_188 = pyc::cpp::Wire<1>((hours < pyc_comb_32) ? 1u : 0u);
    pyc_not_189 = (~pyc_ult_188);
    pyc_zext_190 = pyc::cpp::zext<5, 4>(hr_tens);
    pyc_shli_191 = pyc::cpp::shl<5>(pyc_zext_190, 3u);
    pyc_shli_192 = pyc::cpp::shl<5>(pyc_zext_190, 1u);
    pyc_add_193 = (pyc_shli_191 + pyc_shli_192);
    pyc_sub_194 = (hours - pyc_add_193);
    pyc_trunc_195 = pyc::cpp::trunc<4, 5>(pyc_sub_194);
    pyc_concat_196 = pyc::cpp::concat(hr_tens, pyc_trunc_195);
    pyc_not_197 = (~blink);
    pyc_comb_198 = pyc_not_187;
    pyc_comb_199 = pyc_not_189;
    pyc_comb_200 = pyc_concat_196;
    pyc_comb_201 = pyc_not_197;
  }

  inline void eval_comb_4() {
    pyc_mux_224 = (pyc_and_204.toBool() ? pyc_comb_132 : seconds);
    pyc_mux_225 = (pyc_and_207.toBool() ? pyc_comb_132 : pyc_mux_224);
    pyc_mux_226 = (pyc_and_210.toBool() ? pyc_comb_133 : pyc_mux_225);
    pyc_comb_227 = pyc_mux_226;
  }

  inline void eval_comb_5() {
    pyc_mux_229 = (pyc_comb_134.toBool() ? pyc_comb_135 : minutes);
    pyc_mux_230 = (pyc_and_213.toBool() ? pyc_comb_135 : pyc_mux_229);
    pyc_mux_231 = (pyc_and_216.toBool() ? pyc_comb_136 : pyc_mux_230);
    pyc_comb_232 = pyc_mux_231;
  }

  inline void eval_comb_6() {
    pyc_mux_234 = (pyc_comb_137.toBool() ? pyc_comb_138 : hours);
    pyc_mux_235 = (pyc_and_219.toBool() ? pyc_comb_138 : pyc_mux_234);
    pyc_mux_236 = (pyc_and_222.toBool() ? pyc_comb_139 : pyc_mux_235);
    pyc_comb_237 = pyc_mux_236;
  }

  inline void eval_comb_7() {
    pyc_mux_255 = (pyc_comb_157.toBool() ? pyc_comb_43 : pyc_comb_44);
    pyc_mux_256 = (pyc_comb_158.toBool() ? pyc_comb_41 : pyc_mux_255);
    pyc_mux_257 = (pyc_comb_159.toBool() ? pyc_comb_39 : pyc_mux_256);
    pyc_mux_258 = (pyc_comb_160.toBool() ? pyc_comb_37 : pyc_mux_257);
    pyc_mux_259 = (pyc_comb_161.toBool() ? pyc_comb_35 : pyc_mux_258);
    pyc_comb_260 = pyc_mux_259;
  }

  inline void eval_comb_8() {
    pyc_mux_261 = (pyc_comb_180.toBool() ? pyc_comb_43 : pyc_comb_44);
    pyc_mux_262 = (pyc_comb_181.toBool() ? pyc_comb_41 : pyc_mux_261);
    pyc_mux_263 = (pyc_comb_182.toBool() ? pyc_comb_39 : pyc_mux_262);
    pyc_mux_264 = (pyc_comb_183.toBool() ? pyc_comb_37 : pyc_mux_263);
    pyc_mux_265 = (pyc_comb_184.toBool() ? pyc_comb_35 : pyc_mux_264);
    pyc_comb_266 = pyc_mux_265;
  }

  inline void eval_comb_9() {
    pyc_mux_267 = (pyc_comb_198.toBool() ? pyc_comb_43 : pyc_comb_44);
    pyc_mux_268 = (pyc_comb_199.toBool() ? pyc_comb_41 : pyc_mux_267);
    pyc_comb_269 = pyc_mux_268;
  }

  inline void eval_comb_10() {
    pyc_constant_1 = pyc::cpp::Wire<1>({0x0ull});
    pyc_constant_2 = pyc::cpp::Wire<1>({0x1ull});
    pyc_constant_3 = pyc::cpp::Wire<5>({0x14ull});
    pyc_constant_4 = pyc::cpp::Wire<5>({0xAull});
    pyc_constant_5 = pyc::cpp::Wire<6>({0x32ull});
    pyc_constant_6 = pyc::cpp::Wire<4>({0x5ull});
    pyc_constant_7 = pyc::cpp::Wire<6>({0x28ull});
    pyc_constant_8 = pyc::cpp::Wire<4>({0x4ull});
    pyc_constant_9 = pyc::cpp::Wire<6>({0x1Eull});
    pyc_constant_10 = pyc::cpp::Wire<4>({0x3ull});
    pyc_constant_11 = pyc::cpp::Wire<6>({0x14ull});
    pyc_constant_12 = pyc::cpp::Wire<4>({0x2ull});
    pyc_constant_13 = pyc::cpp::Wire<6>({0xAull});
    pyc_constant_14 = pyc::cpp::Wire<4>({0x1ull});
    pyc_constant_15 = pyc::cpp::Wire<4>({0x0ull});
    pyc_constant_16 = pyc::cpp::Wire<5>({0x17ull});
    pyc_constant_17 = pyc::cpp::Wire<6>({0x1ull});
    pyc_constant_18 = pyc::cpp::Wire<6>({0x0ull});
    pyc_constant_19 = pyc::cpp::Wire<6>({0x3Bull});
    pyc_constant_20 = pyc::cpp::Wire<2>({0x3ull});
    pyc_constant_21 = pyc::cpp::Wire<2>({0x2ull});
    pyc_constant_22 = pyc::cpp::Wire<2>({0x1ull});
    pyc_constant_23 = pyc::cpp::Wire<2>({0x0ull});
    pyc_constant_24 = pyc::cpp::Wire<10>({0x1ull});
    pyc_constant_25 = pyc::cpp::Wire<10>({0x0ull});
    pyc_constant_26 = pyc::cpp::Wire<10>({0x3E7ull});
    pyc_constant_27 = pyc::cpp::Wire<5>({0x1ull});
    pyc_constant_28 = pyc::cpp::Wire<5>({0x0ull});
    pyc_constant_29 = pyc::cpp::Wire<5>({0x13ull});
    pyc_comb_30 = pyc_constant_1;
    pyc_comb_31 = pyc_constant_2;
    pyc_comb_32 = pyc_constant_3;
    pyc_comb_33 = pyc_constant_4;
    pyc_comb_34 = pyc_constant_5;
    pyc_comb_35 = pyc_constant_6;
    pyc_comb_36 = pyc_constant_7;
    pyc_comb_37 = pyc_constant_8;
    pyc_comb_38 = pyc_constant_9;
    pyc_comb_39 = pyc_constant_10;
    pyc_comb_40 = pyc_constant_11;
    pyc_comb_41 = pyc_constant_12;
    pyc_comb_42 = pyc_constant_13;
    pyc_comb_43 = pyc_constant_14;
    pyc_comb_44 = pyc_constant_15;
    pyc_comb_45 = pyc_constant_16;
    pyc_comb_46 = pyc_constant_17;
    pyc_comb_47 = pyc_constant_18;
    pyc_comb_48 = pyc_constant_19;
    pyc_comb_49 = pyc_constant_20;
    pyc_comb_50 = pyc_constant_21;
    pyc_comb_51 = pyc_constant_22;
    pyc_comb_52 = pyc_constant_23;
    pyc_comb_53 = pyc_constant_24;
    pyc_comb_54 = pyc_constant_25;
    pyc_comb_55 = pyc_constant_26;
    pyc_comb_56 = pyc_constant_27;
    pyc_comb_57 = pyc_constant_28;
    pyc_comb_58 = pyc_constant_29;
  }

  inline void eval_comb_11() {
    pyc_eq_59 = pyc::cpp::Wire<1>((btn_set == db_set_prev) ? 1u : 0u);
    pyc_not_60 = (~pyc_eq_59);
    pyc_eq_61 = pyc::cpp::Wire<1>((db_set_cnt == pyc_comb_58) ? 1u : 0u);
    pyc_add_62 = (db_set_cnt + pyc_comb_56);
    pyc_mux_63 = (pyc_eq_61.toBool() ? db_set_cnt : pyc_add_62);
    pyc_mux_64 = (pyc_not_60.toBool() ? pyc_comb_57 : pyc_mux_63);
    pyc_mux_65 = (pyc_eq_61.toBool() ? btn_set : db_set_stable);
    pyc_not_66 = (~db_set_stable_prev);
    pyc_and_67 = (db_set_stable & pyc_not_66);
    pyc_comb_68 = pyc_mux_64;
    pyc_comb_69 = pyc_mux_65;
    pyc_comb_70 = pyc_and_67;
  }

  inline void eval_comb_12() {
    pyc_eq_71 = pyc::cpp::Wire<1>((btn_plus == db_plus_prev) ? 1u : 0u);
    pyc_not_72 = (~pyc_eq_71);
    pyc_eq_73 = pyc::cpp::Wire<1>((db_plus_cnt == pyc_comb_58) ? 1u : 0u);
    pyc_add_74 = (db_plus_cnt + pyc_comb_56);
    pyc_mux_75 = (pyc_eq_73.toBool() ? db_plus_cnt : pyc_add_74);
    pyc_mux_76 = (pyc_not_72.toBool() ? pyc_comb_57 : pyc_mux_75);
    pyc_mux_77 = (pyc_eq_73.toBool() ? btn_plus : db_plus_stable);
    pyc_not_78 = (~db_plus_stable_prev);
    pyc_and_79 = (db_plus_stable & pyc_not_78);
    pyc_comb_80 = pyc_mux_76;
    pyc_comb_81 = pyc_mux_77;
    pyc_comb_82 = pyc_and_79;
  }

  inline void eval_comb_pass() {
    eval_comb_10();
    eval_comb_11();
    eval_comb_12();
    eval_comb_0();
    eval_comb_1();
    eval_comb_2();
    eval_comb_3();
    pyc_and_204 = (pyc_reg_202 & pyc_reg_203);
    pyc_and_207 = (pyc_reg_205 & pyc_reg_206);
    pyc_and_210 = (pyc_reg_208 & pyc_reg_209);
    pyc_and_213 = (pyc_reg_211 & pyc_reg_212);
    pyc_and_216 = (pyc_reg_214 & pyc_reg_215);
    pyc_and_219 = (pyc_reg_217 & pyc_reg_218);
    pyc_and_222 = (pyc_reg_220 & pyc_reg_221);
    prescaler = pyc_reg_223;
    eval_comb_4();
    seconds = pyc_reg_228;
    eval_comb_5();
    minutes = pyc_reg_233;
    eval_comb_6();
    hours = pyc_reg_238;
    pyc_mux_239 = (pyc_comb_70.toBool() ? pyc_comb_131 : mode);
    mode = pyc_reg_240;
    pyc_mux_241 = (pyc_comb_125.toBool() ? pyc_comb_201 : blink);
    blink = pyc_reg_242;
    db_set_cnt = pyc_reg_243;
    db_set_prev = pyc_reg_244;
    db_set_stable = pyc_reg_245;
    db_set_stable_prev = pyc_reg_246;
    db_plus_cnt = pyc_reg_247;
    db_plus_prev = pyc_reg_248;
    db_plus_stable = pyc_reg_249;
    db_plus_stable_prev = pyc_reg_250;
    db_minus_cnt = pyc_reg_251;
    db_minus_prev = pyc_reg_252;
    db_minus_stable = pyc_reg_253;
    db_minus_stable_prev = pyc_reg_254;
    eval_comb_7();
    sec_tens = pyc_comb_260;
    eval_comb_8();
    min_tens = pyc_comb_266;
    eval_comb_9();
    hr_tens = pyc_comb_269;
  }

  void eval() {
    eval_comb_pass();
    hours_bcd = pyc_comb_200;
    minutes_bcd = pyc_comb_185;
    seconds_bcd = pyc_comb_162;
    setting_mode = mode;
    colon_blink = blink;
  }

  void tick() {
    // Two-phase update: compute next state for all sequential elements,
    // then commit together. This avoids ordering artifacts between regs.
    // Phase 1: compute.
    pyc_reg_202_inst.tick_compute();
    pyc_reg_203_inst.tick_compute();
    pyc_reg_205_inst.tick_compute();
    pyc_reg_206_inst.tick_compute();
    pyc_reg_208_inst.tick_compute();
    pyc_reg_209_inst.tick_compute();
    pyc_reg_211_inst.tick_compute();
    pyc_reg_212_inst.tick_compute();
    pyc_reg_214_inst.tick_compute();
    pyc_reg_215_inst.tick_compute();
    pyc_reg_217_inst.tick_compute();
    pyc_reg_218_inst.tick_compute();
    pyc_reg_220_inst.tick_compute();
    pyc_reg_221_inst.tick_compute();
    pyc_reg_223_inst.tick_compute();
    pyc_reg_228_inst.tick_compute();
    pyc_reg_233_inst.tick_compute();
    pyc_reg_238_inst.tick_compute();
    pyc_reg_240_inst.tick_compute();
    pyc_reg_242_inst.tick_compute();
    pyc_reg_243_inst.tick_compute();
    pyc_reg_244_inst.tick_compute();
    pyc_reg_245_inst.tick_compute();
    pyc_reg_246_inst.tick_compute();
    pyc_reg_247_inst.tick_compute();
    pyc_reg_248_inst.tick_compute();
    pyc_reg_249_inst.tick_compute();
    pyc_reg_250_inst.tick_compute();
    pyc_reg_251_inst.tick_compute();
    pyc_reg_252_inst.tick_compute();
    pyc_reg_253_inst.tick_compute();
    pyc_reg_254_inst.tick_compute();
    // Phase 2: commit.
    pyc_reg_202_inst.tick_commit();
    pyc_reg_203_inst.tick_commit();
    pyc_reg_205_inst.tick_commit();
    pyc_reg_206_inst.tick_commit();
    pyc_reg_208_inst.tick_commit();
    pyc_reg_209_inst.tick_commit();
    pyc_reg_211_inst.tick_commit();
    pyc_reg_212_inst.tick_commit();
    pyc_reg_214_inst.tick_commit();
    pyc_reg_215_inst.tick_commit();
    pyc_reg_217_inst.tick_commit();
    pyc_reg_218_inst.tick_commit();
    pyc_reg_220_inst.tick_commit();
    pyc_reg_221_inst.tick_commit();
    pyc_reg_223_inst.tick_commit();
    pyc_reg_228_inst.tick_commit();
    pyc_reg_233_inst.tick_commit();
    pyc_reg_238_inst.tick_commit();
    pyc_reg_240_inst.tick_commit();
    pyc_reg_242_inst.tick_commit();
    pyc_reg_243_inst.tick_commit();
    pyc_reg_244_inst.tick_commit();
    pyc_reg_245_inst.tick_commit();
    pyc_reg_246_inst.tick_commit();
    pyc_reg_247_inst.tick_commit();
    pyc_reg_248_inst.tick_commit();
    pyc_reg_249_inst.tick_commit();
    pyc_reg_250_inst.tick_commit();
    pyc_reg_251_inst.tick_commit();
    pyc_reg_252_inst.tick_commit();
    pyc_reg_253_inst.tick_commit();
    pyc_reg_254_inst.tick_commit();
  }
};

} // namespace pyc::gen
