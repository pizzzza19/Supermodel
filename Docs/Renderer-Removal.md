# 描画エンジンの整理（2026-10-28）

macOS は Metal、Windows / Linux は Vulkan に固定しました。起動時の
エンジン選択と OpenGL へのフォールバックはありません。

## 削除内容

- New3D / Legacy3D の OpenGL 描画実装、GLSL シェーダーと Quad Rendering。
- GLEW、OpenGL のフレームバッファー、SuperAA / CRT 色補正、外部シェーダー。
- OpenGL 専用のクロスヘア描画とスクリーンショット操作。
- Pro-1000 エミュレーターと Windows の ASPI ホスト DLL。
- 各OSの OpenGL / GLUT / GLU リンク指定と関連する設定・CLI。

削除前のソース目録と比較して74ファイルを削除しました。

## 残したものと移行先

- `Graphics/New3D/` の数学、モデル形式、ポリゴンヘッダーの解析は
  Metal / Vulkan 共通の CPU コードなので残しています。元データの四角形は
  読み取り後に三角形へ分割します。Quad Rendering シェーダーはありません。
- ランチャーは Dear ImGui の SDL_Renderer バックエンドへ移行しました。
  macOS は Metal、Windows は Direct3D、Linux はソフトウェア描画を明示的に
  選択します。macOS / Windows のランチャーにはソフトウェアの代替もあります。
- 旧INIのエンジン選択・Quad・CRT・SuperAAなどの項目は読み込み時に無効化し、
  ランチャーで保存した際は書き出しません。他の設定とゲーム別セクションは保持します。
- Model 3 のゲーム、入力設定、ライトガン入力、Vulkan の実装は維持しています。

## 確認結果

- macOS の通常ビルド：成功。
- 新規オブジェクトディレクトリーでデバッガー・追加デバッグ有効ビルド：成功。
- `test-metal`：成功（部分転送、描画、リサイズ、コンテキスト再作成）。
- ランチャー：Metal バックエンドの初期化をログで確認。
- VF3A：28秒の起動・フレーム処理・正常終了を確認。
- 本体のリンク先と未解決シンボル：OpenGL / GLUT / GLU / GLEW 依存なし。
- Windows の現行 MSVC プロジェクト：XML とソース参照の存在を確認。
- 各 Makefile：OpenGL のリンク指定なし。
- CLI：旧描画オプションがヘルプから消えたことを確認。

2026-10-29 に Windows x64 Release のビルドと、Vulkan の ROM 不要テスト
（4フィルター、1328フレーム、1920x1200、リサイズ・再初期化）を確認しました。
ランチャーの描画倍率テストも1倍・1.5倍・2倍で成功しました。
MSVC プロジェクトに68000生成コード4ファイルの登録漏れがあったため復元しています。
Windows の実ゲーム動作と Linux 上でのコンパイル・GPU動作は未確認です。
ランチャーの画面操作は Computer Use のアプリ許可が得られず未確認です。
VF3A の対戦全場面、他ゲームの描画精度は今回の確認範囲に含まれません。
既存の macOS SDK / deployment target に関するビルド警告は残っています。

## 削除ファイル一覧

- `Src/Graphics/FBO.cpp`
- `Src/Graphics/FBO.h`
- `Src/Graphics/Legacy3D/Error.cpp`
- `Src/Graphics/Legacy3D/Legacy3D.cpp`
- `Src/Graphics/Legacy3D/Legacy3D.h`
- `Src/Graphics/Legacy3D/Models.cpp`
- `Src/Graphics/Legacy3D/Shaders/DIR.txt`
- `Src/Graphics/Legacy3D/Shaders/Fragment.glsl`
- `Src/Graphics/Legacy3D/Shaders/Fragment2D.glsl`
- `Src/Graphics/Legacy3D/Shaders/Fragment_Flat.glsl`
- `Src/Graphics/Legacy3D/Shaders/Fragment_MultiSheet.glsl`
- `Src/Graphics/Legacy3D/Shaders/Fragment_NoSpotlight.glsl`
- `Src/Graphics/Legacy3D/Shaders/Vertex.glsl`
- `Src/Graphics/Legacy3D/Shaders/Vertex2D.glsl`
- `Src/Graphics/Legacy3D/Shaders3D.h`
- `Src/Graphics/Legacy3D/TextureRefs.cpp`
- `Src/Graphics/Legacy3D/TextureRefs.h`
- `Src/Graphics/New3D/GLSLShader.cpp`
- `Src/Graphics/New3D/GLSLShader.h`
- `Src/Graphics/New3D/New3D.cpp`
- `Src/Graphics/New3D/New3D.h`
- `Src/Graphics/New3D/R3DFrameBuffers.cpp`
- `Src/Graphics/New3D/R3DFrameBuffers.h`
- `Src/Graphics/New3D/R3DScrollFog.cpp`
- `Src/Graphics/New3D/R3DScrollFog.h`
- `Src/Graphics/New3D/R3DShader.cpp`
- `Src/Graphics/New3D/R3DShader.h`
- `Src/Graphics/New3D/R3DShaderCommon.h`
- `Src/Graphics/New3D/R3DShaderQuads.h`
- `Src/Graphics/New3D/R3DShaderTriangles.h`
- `Src/Graphics/New3D/TextureBank.cpp`
- `Src/Graphics/New3D/TextureBank.h`
- `Src/Graphics/New3D/VBO.cpp`
- `Src/Graphics/New3D/VBO.h`
- `Src/Graphics/Pro1000Render2D.cpp`
- `Src/Graphics/Pro1000Render2D.h`
- `Src/Graphics/Render2D.cpp`
- `Src/Graphics/Render2D.h`
- `Src/Graphics/Shader.cpp`
- `Src/Graphics/Shader.h`
- `Src/Graphics/Shaders2D.h`
- `Src/Graphics/SuperAA.cpp`
- `Src/Graphics/SuperAA.h`
- `Src/OSD/SDL/Crosshair.cpp`
- `Src/OSD/SDL/Crosshair.h`
- `Src/OSD/SDL/PolyAnalysis.h`
- `Src/OSD/Windows/wnaspi32/ConnectDialog.cpp`
- `Src/OSD/Windows/wnaspi32/ConnectDialog.h`
- `Src/OSD/Windows/wnaspi32/aspi.h`
- `Src/OSD/Windows/wnaspi32/wnaspi32.cpp`
- `Src/OSD/Windows/wnaspi32/wnaspi32.def`
- `Src/Pkgs/GL/glew.h`
- `Src/Pkgs/GL/glxew.h`
- `Src/Pkgs/GL/wglew.h`
- `Src/Pkgs/glew.c`
- `Src/Pkgs/imgui/imgui_impl_opengl3.cpp`
- `Src/Pkgs/imgui/imgui_impl_opengl3.h`
- `Src/Pkgs/imgui/imgui_impl_opengl3_loader.h`
- `Src/Pkgs/wglew.h`
- `Src/Pro1000/8259.cpp`
- `Src/Pro1000/8259.h`
- `Src/Pro1000/Pro1000.cpp`
- `Src/Pro1000/Pro1000.h`
- `Src/Pro1000/SCANPSC100F.cpp`
- `Src/Pro1000/SCANPSC100F.h`
- `Src/Pro1000/SCSIHostAdapter.cpp`
- `Src/Pro1000/SCSIHostAdapter.h`
- `Src/Pro1000/SCSITCPBridge.cpp`
- `Src/Pro1000/SCSITCPBridge.h`
- `Src/Pro1000/SCSITCPBridgeProtocol.h`
- `Src/Pro1000/SyntheticSCSIROM.cpp`
- `Src/Pro1000/SyntheticSCSIROM.h`
- `Src/Pro1000/UART.cpp`
- `Src/Pro1000/UART.h`

## Retina ランチャー表示の修正

SDL_Renderer への移行時に描画座標への framebuffer scale 適用が不足しており、
Retina で内容が縦横半分（面積1/4）になっていました。SDL の描画倍率を適用する
処理を追加しました。`make -f Makefiles/Makefile.OSX test-launcher` はオフスクリーンで
1倍・1.5倍・2倍の描画到達範囲とクリッピングを確認します。全倍率で通過しました。
