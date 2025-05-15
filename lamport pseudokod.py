init_MPI


id = id_procesu
stan = "IDLE"
K = liczba_doków
M = liczba_mechaników
timestamp = 0
lista_requestow = []
ACK_licznik = 0
ACK_max = liczba_procesow - 1

while True:
	if stan =="IDLE":
		if potrzeba_naprawy():
			timestamp += 1
			Z = losowa_liczba(1, Z_MAX) #Z - mechanicy którzy wymagani do naprawa, możemy albo Z_MAX ustalić, albo poprostu żeby losowało od 1 do M idk
			dodaj_do_swojego_queue(timestamp, id_swoje, Z_swoje)
			send_request_to_all(timestamp, id_swoje, Z_swoje) #REQ
			ACK_licznik = 0 #zeracja przy kazdej potrzebie naprawy
			stan = "WAITING_FOR_REPLIES"
		else: 
			timestamp += 1
			cziling_bomba()


	if stan == "WAITING_FOR_REPLIES":
		timestamp += 1
		if ACK_licznik == ACK_max:
			sort_kolejke()
			if queue[moje_id].pos <= K && suma_Z_above() + Z <= M:
				stan = "IN_CRITICAL_SECTION"
			             


	if stan == "IN_CRITICAL_SECTION":
		naprawa()
		timestamp += 1
		send_release_reply(swoje_id)
		remove_from_queue(swije_id)
		stan == "IDLE"



	#Asynchronicznie komunikaty obsługiwać

	on RECEIVE(REQUEST):
		my_timestamp = max(my_timestamp, mess_timestamp) + 1
		add_to_queue(mess_timestamp, mess_ID, mess_Z)
		sort_queue() #po timestamp, if mess_timestamp == timestamp w queue, po id kolejkuje parami
		send_ACK(mess_ID)
	
	on RECEIVE(ACK):
		my_timestamp = max(my_timestamp, mess_timestamp) + 1
		ACK_licznik += 1
	
	on RECEIVE(RELEASE):
		my_timestamp = max(my_timestamp, mess_timestamp) + 1
		remove_from_queue(ID)

	

finish_MPI


