const functions = require('firebase-functions/v1');
const admin = require('firebase-admin');
admin.initializeApp();

exports.sendWindAlarm = functions.database.instance('wind-alarm-default-rtdb').ref('/live/alarm_triggered')
    .onUpdate(async (change, context) => {
        
        const isAlarmNow = change.after.val();
        const wasAlarmBefore = change.before.val();

        if (isAlarmNow === true && wasAlarmBefore === false) {
            
            try {
                // 1. Hent den besked, der står under live/message
                // Vi bruger admin.database() til at kigge et andet sted i databasen
                const messageSnapshot = await admin.database()
                    .ref('/live/message')
                    .once('value');
                
                const customMessage = messageSnapshot.val() || "Vinden er for kraftig!";

                // 2. Definer beskeden med teksten fra databasen
                const message = {
                    notification: {
                        title: '⚠️ Vind Alarm!',
                        body: customMessage // Her indsætter vi din live/message streng
                    },
                    topic: 'wind_alerts',
                };

                // 3. Send notifikationen
                const response = await admin.messaging().send(message);
                console.log('Notifikation sendt med besked:', customMessage);
                return null;
                
            } catch (error) {
                console.error('Fejl:', error);
                return null;
            }
        }
        return null;
    });