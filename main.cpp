#include <iostream>
#include <cstdlib>
#include <string>
// httplib는 헤더 온리 라이브러리입니다. (아래 Dockerfile에서 다운로드 처리함)
#include "httplib.h"

int main() {
    httplib::Server svr;

    // 1. /ping 경로에 대한 GET 요청 처리
    svr.Get("/ping", [](const httplib::Request&, httplib::Response& res) {
        // 응답 본문과 컨텐츠 타입 설정
        res.set_content("pong", "text/plain");
        std::cout << "[INFO] Received a ping request" << std::endl;
    });

    // 2. 헬스 체크용 루트 경로 (옵션)
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("Server is running. Send request to /ping", "text/plain");
    });

    // 3. GCP Cloud Run은 'PORT' 환경 변수를 주입합니다. 없으면 8080 사용.
    const char* port_env = std::getenv("PORT");
    int port = port_env ? std::stoi(port_env) : 8080;

    std::cout << "Server is starting on port " << port << "..." << std::endl;

    // 4. 서버 시작 (0.0.0.0으로 바인딩해야 컨테이너 외부에서 접근 가능)
    // listen은 블로킹 함수이므로 여기서 프로그램이 계속 실행됩니다.
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "[ERROR] Failed to bind to port " << port << std::endl;
        return 1;
    }

    return 0;
}