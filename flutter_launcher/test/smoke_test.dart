import 'package:flutter_test/flutter_test.dart';
import 'package:kirikiri_launcher/main.dart';

void main() {
  testWidgets('launcher renders app shell', (tester) async {
    await tester.pumpWidget(const KiriKiriLauncherApp());
    expect(find.text('KiriKiri Launcher'), findsWidgets);
  });
}
