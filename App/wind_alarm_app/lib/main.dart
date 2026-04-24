import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:firebase_messaging/firebase_messaging.dart';
import 'package:flutter/material.dart';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';
import 'firebase_options.dart';

// --- BAGGRUNDS HANDLER ---
@pragma('vm:entry-point')
Future<void> _firebaseMessagingBackgroundHandler(RemoteMessage message) async {
  await Firebase.initializeApp(options: DefaultFirebaseOptions.currentPlatform);
  print("FCM Baggrundsbesked modtaget: ${message.messageId}");
}

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await Firebase.initializeApp(
    options: DefaultFirebaseOptions.currentPlatform,
  );
  
  FirebaseMessaging.onBackgroundMessage(_firebaseMessagingBackgroundHandler);
  
  runApp(const WindAlarmApp());
}

class WindAlarmApp extends StatelessWidget {
  const WindAlarmApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Wind Alarm',
      theme: ThemeData(primarySwatch: Colors.blue, useMaterial3: true),
      home: const Dashboard(),
    );
  }
}

class Dashboard extends StatefulWidget {
  const Dashboard({super.key});

  @override
  State<Dashboard> createState() => _DashboardState();
}

class _DashboardState extends State<Dashboard> {
  final DatabaseReference _dbRef = FirebaseDatabase.instanceFor(
    app: Firebase.app(),
    databaseURL: "https://wind-alarm-default-rtdb.europe-west1.firebasedatabase.app",
  ).ref();

  final FlutterLocalNotificationsPlugin _notificationsPlugin = FlutterLocalNotificationsPlugin();
  final FirebaseMessaging _messaging = FirebaseMessaging.instance;

  @override
  void initState() {
    super.initState();
    _initLocalNotifications(); // Nødvendig for notifikationer mens appen er åben
    _setupFCM();               // Nødvendig for at lytte til skyen
  }

  void _setupFCM() async {
    NotificationSettings settings = await _messaging.requestPermission(
      alert: true,
      badge: true,
      sound: true,
    );

    if (settings.authorizationStatus == AuthorizationStatus.authorized) {
      await _messaging.subscribeToTopic('wind_alerts');
      print("Abonnerer på wind_alerts!");

      // Fanger beskeder når appen er ÅBEN og viser dem manuelt
      FirebaseMessaging.onMessage.listen((RemoteMessage message) {
        if (message.notification != null) {
          _showNotification(
            message.notification!.title ?? "FCM Alarm",
            message.notification!.body ?? "",
          );
        }
      });
    }
  }

  void _initLocalNotifications() async {
    const AndroidInitializationSettings initializationSettingsAndroid =
        AndroidInitializationSettings('@mipmap/ic_launcher');
    
    const DarwinInitializationSettings initializationSettingsIOS =
        DarwinInitializationSettings(
            requestAlertPermission: true,
            requestBadgePermission: true,
            requestSoundPermission: true,
        );
        
    const InitializationSettings initializationSettings = InitializationSettings(
      android: initializationSettingsAndroid,
      iOS: initializationSettingsIOS,
    );
    
    await _notificationsPlugin.initialize(settings: initializationSettings);
  }

  Future<void> _showNotification(String title, String body) async {
    const AndroidNotificationDetails androidDetails = AndroidNotificationDetails(
      'wind_alarm_channel', 
      'Vind Alarmer',
      importance: Importance.max,
      priority: Priority.high,
      color: Colors.red,
    );
    
    const NotificationDetails platformDetails = NotificationDetails(android: androidDetails);
    
    await _notificationsPlugin.show(
      id: 0, 
      title: title, 
      body: body, 
      notificationDetails: platformDetails,
    );
  }

  Color _getColorForWind(double wind) {
    double normalized = (wind / 10.0).clamp(0.0, 1.0);

    if (normalized < 0.5) {
      return Color.lerp(Colors.green.shade300, Colors.yellow.shade300, normalized * 2)!;
    } else {
      return Color.lerp(Colors.yellow.shade300, Colors.red.shade400, (normalized - 0.5) * 2)!;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text("🌬️ Wind Alarm Monitor")),
      body: StreamBuilder(
        stream: _dbRef.onValue,
        builder: (context, snapshot) {
          if (snapshot.hasData && snapshot.data!.snapshot.value != null) {
            Map<dynamic, dynamic> data = snapshot.data!.snapshot.value as Map<dynamic, dynamic>;

            Map<dynamic, dynamic>? liveData = data['live'] as Map<dynamic, dynamic>?;
            double currentWind = (liveData?['wind_speed'] ?? 0).toDouble();
            bool isAlarm = liveData?['alarm_triggered'] == true;

            Map<dynamic, dynamic>? forecastData = data['vejr_prognose'] as Map<dynamic, dynamic>?;
            List<MapEntry<String, dynamic>> sortedForecast = [];
            
            if (forecastData != null) {
              sortedForecast = forecastData.entries
                  .map((e) => MapEntry(e.key.toString(), e.value))
                  .toList();
              sortedForecast.sort((a, b) => a.key.compareTo(b.key));
            }

            return Column(
              children: [
                Container(
                  width: double.infinity,
                  padding: const EdgeInsets.all(20),
                  color: isAlarm ? Colors.red.shade100 : Colors.blue.shade50,
                  child: Column(
                    children: [
                      Text(
                        "Nuværende Vindhastighed",
                        style: TextStyle(fontSize: 18, color: isAlarm ? Colors.red : Colors.blueGrey),
                      ),
                      Text(
                        "$currentWind m/s",
                        style: TextStyle(
                            fontSize: 48, 
                            fontWeight: FontWeight.bold, 
                            color: isAlarm ? Colors.red.shade900 : Colors.blue),
                      ),
                      if (isAlarm && liveData?['message'] != null) ...[
                        const SizedBox(height: 10),
                        Text(
                          liveData!['message'],
                          style: const TextStyle(color: Colors.red, fontWeight: FontWeight.bold),
                        ),
                      ]
                    ],
                  ),
                ),
                
                const Padding(
                  padding: EdgeInsets.all(16.0),
                  child: Text(
                    "Vejrprognose", 
                    style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)
                  ),
                ),

                Expanded(
                  child: sortedForecast.isEmpty 
                    ? const Center(child: Text("Ingen prognosedata fundet."))
                    : ListView.builder(
                    itemCount: sortedForecast.length,
                    itemBuilder: (context, index) {
                      var entry = sortedForecast[index];
                      String timeString = entry.key; 
                      
                      DateTime dt = DateTime.parse(timeString);
                      String formattedTime = "${dt.hour.toString().padLeft(2, '0')}:${dt.minute.toString().padLeft(2, '0')}";

                      double wind = (entry.value['wind'] ?? 0).toDouble();
                      double gust = (entry.value['gust'] ?? 0).toDouble();

                      Color rowColor = _getColorForWind(wind);

                      return Card(
                        margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
                        color: rowColor,
                        child: ListTile(
                          leading: const Icon(Icons.air, color: Colors.black54),
                          title: Text("Kl. $formattedTime", style: const TextStyle(fontWeight: FontWeight.bold)),
                          subtitle: Text("Vind: ${wind.toStringAsFixed(1)} m/s | Vindstød: ${gust.toStringAsFixed(1)} m/s"),
                        ),
                      );
                    },
                  ),
                ),
              ],
            );
          } else if (snapshot.hasError) {
            return Center(child: Text("Fejl: ${snapshot.error}"));
          }
          return const Center(child: CircularProgressIndicator());
        },
      ),
    );
  }
}