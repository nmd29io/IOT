FROM node:22-alpine
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --omit=dev
COPY server.js nodes.json ./
COPY public ./public
ENV NODE_ENV=production PORT=8080
EXPOSE 8080
CMD ["node", "--no-warnings", "server.js"]
